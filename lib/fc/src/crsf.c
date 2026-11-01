/*
 * CRSF framer, decoders and conversion. See fc/crsf.h for the design decisions.
 */
#include <fc/crsf.h>

#include <string.h>

/* Framer states, stored in fc_crsf_parser_t.state. */
#define ST_HUNT 0U /* waiting for a sync byte */
#define ST_LEN  1U /* sync seen, waiting for the length byte */
#define ST_BODY 2U /* collecting type + payload + crc */

static const uint8_t crc8_table[256] = {
	0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
	0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
	0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
	0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
	0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
	0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
	0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
	0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
	0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
	0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
	0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
	0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
	0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
	0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
	0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
	0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9,
};

static bool sync_accepted(uint8_t byte)
{
	return byte == FC_CRSF_SYNC_FC;
}

/* Start a frame if byte is an accepted sync byte. */
static bool start_frame(fc_crsf_parser_t *p, uint8_t byte)
{
	if (!sync_accepted(byte)) {
		return false;
	}

	p->sync = byte;
	p->state = ST_LEN;
	return true;
}

static int8_t to_i8(uint8_t value)
{
	/* Avoids the implementation-defined out-of-range uint8_t to int8_t conversion. */
	return (int8_t)((value & 0x80U) != 0U ? (int)value - 256 : (int)value);
}

uint8_t fc_crsf_crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0U;

	if (data == NULL) {
		return 0U;
	}

	for (size_t i = 0U; i < len; i++) {
		crc = crc8_table[crc ^ data[i]];
	}

	return crc;
}

void fc_crsf_parser_init(fc_crsf_parser_t *p)
{
	if (p == NULL) {
		return;
	}

	memset(p, 0, sizeof(*p));
	p->state = ST_HUNT;
}

void fc_crsf_parser_reset(fc_crsf_parser_t *p)
{
	if (p == NULL) {
		return;
	}

	p->state = ST_HUNT;
	p->len = 0U;
	p->idx = 0U;
}

fc_crsf_event_t fc_crsf_parser_feed(fc_crsf_parser_t *p, uint8_t byte, fc_crsf_frame_t *frame)
{
	if (p == NULL || frame == NULL) {
		return FC_CRSF_EVT_NONE;
	}

	switch (p->state) {
	case ST_LEN:
		if (byte < FC_CRSF_LEN_MIN || byte > FC_CRSF_LEN_MAX) {
			p->stats.len_errors++;
			p->state = ST_HUNT;
			/* The rejected byte may itself be the start of the next frame. */
			(void)start_frame(p, byte);
			return FC_CRSF_EVT_NONE;
		}

		p->len = byte;
		p->idx = 0U;
		p->state = ST_BODY;
		return FC_CRSF_EVT_NONE;

	case ST_BODY:
		p->buf[p->idx] = byte;
		p->idx++;
		if (p->idx < p->len) {
			return FC_CRSF_EVT_NONE;
		}

		p->state = ST_HUNT;
		if (fc_crsf_crc8(p->buf, (size_t)p->len - 1U) != p->buf[p->len - 1U]) {
			p->stats.crc_errors++;
			return FC_CRSF_EVT_NONE;
		}

		p->stats.frames_ok++;
		frame->sync = p->sync;
		frame->type = p->buf[0];
		frame->payload = &p->buf[1];
		frame->payload_len = (uint8_t)(p->len - 2U);
		return FC_CRSF_EVT_FRAME;

	case ST_HUNT:
	default:
		p->state = ST_HUNT;
		if (!start_frame(p, byte)) {
			p->stats.garbage_bytes++;
		}
		return FC_CRSF_EVT_NONE;
	}
}

const fc_crsf_stats_t *fc_crsf_parser_stats(const fc_crsf_parser_t *p)
{
	if (p == NULL) {
		return NULL;
	}

	return &p->stats;
}

bool fc_crsf_decode_channels(const uint8_t *payload, size_t len, fc_crsf_channels_t *out)
{
	uint32_t acc = 0U;
	unsigned int nbits = 0U;
	size_t n = 0U;

	if (payload == NULL || out == NULL || len < FC_CRSF_CHANNELS_PAYLOAD_LEN) {
		return false;
	}

	/* 22 bytes are exactly 16 x 11 bits. Bits are packed LSB-first. */
	for (size_t i = 0U; i < FC_CRSF_CHANNELS_PAYLOAD_LEN; i++) {
		acc |= (uint32_t)payload[i] << nbits;
		nbits += 8U;

		while (nbits >= 11U && n < FC_CRSF_NUM_CHANNELS) {
			out->ticks[n] = (uint16_t)(acc & FC_CRSF_TICKS_RAW_MAX);
			acc >>= 11;
			nbits -= 11U;
			n++;
		}
	}

	return true;
}

bool fc_crsf_decode_link_stats(const uint8_t *payload, size_t len, fc_crsf_link_stats_t *out)
{
	if (payload == NULL || out == NULL || len < FC_CRSF_LINK_STATS_PAYLOAD_LEN) {
		return false;
	}

	out->uplink_rssi_ant1 = payload[0];
	out->uplink_rssi_ant2 = payload[1];
	out->uplink_lq = payload[2];
	out->uplink_snr = to_i8(payload[3]);
	out->active_antenna = payload[4];
	out->rf_profile = payload[5];
	out->uplink_rf_power = payload[6];
	out->downlink_rssi = payload[7];
	out->downlink_lq = payload[8];
	out->downlink_snr = to_i8(payload[9]);

	return true;
}

uint16_t fc_crsf_ticks_to_us(uint16_t ticks)
{
	int32_t t = (ticks > FC_CRSF_TICKS_RAW_MAX) ? (int32_t)FC_CRSF_TICKS_RAW_MAX : (int32_t)ticks;

	return (uint16_t)(((t - (int32_t)FC_CRSF_TICKS_CENTER) * 5) / 8 + 1500);
}

float fc_crsf_ticks_to_unit(uint16_t ticks)
{
	float value;

	if (ticks >= FC_CRSF_TICKS_CENTER) {
		value = (float)(ticks - FC_CRSF_TICKS_CENTER) /
			(float)(FC_CRSF_TICKS_MAX - FC_CRSF_TICKS_CENTER);
	} else {
		value = -(float)(FC_CRSF_TICKS_CENTER - ticks) /
			(float)(FC_CRSF_TICKS_CENTER - FC_CRSF_TICKS_MIN);
	}

	if (value > 1.0f) {
		value = 1.0f;
	} else if (value < -1.0f) {
		value = -1.0f;
	}

	return value;
}
