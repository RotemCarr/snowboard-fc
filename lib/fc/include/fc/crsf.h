/* lib/fc/include/fc/crsf.h */
#ifndef FC_CRSF_H_
#define FC_CRSF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * CRSF receiver protocol: framing, payload decoding, unit conversion.
 * Pure C, no Zephyr dependencies, no clock, no allocation, no static state.
 * One parser context belongs to one thread (the receiver thread).
 *
 * Decisions: C1..C7 go here once settled.
 */

/* ---- Protocol constants (spec: crsf.md) ---- */

#define FC_CRSF_MAX_FRAME_SIZE 64U /* sync + len + type + payload + crc */
#define FC_CRSF_LEN_MIN        2U  /* len field: type + crc */
#define FC_CRSF_LEN_MAX        62U /* len field: type + payload + crc */
#define FC_CRSF_SYNC_FC        0xC8U

#define FC_CRSF_TYPE_LINK_STATS  0x14U
#define FC_CRSF_TYPE_RC_CHANNELS 0x16U

#define FC_CRSF_NUM_CHANNELS            16U
#define FC_CRSF_CHANNELS_PAYLOAD_LEN    22U /* minimum */
#define FC_CRSF_LINK_STATS_PAYLOAD_LEN  10U /* minimum */

/* Channel ticks. Raw range is 11 bits; nominal travel and centre are NOT in the
 * spec's text (centre is), verify against a real receiver. */
#define FC_CRSF_TICKS_RAW_MAX 2047U
#define FC_CRSF_TICKS_MIN     172U
#define FC_CRSF_TICKS_CENTER  992U
#define FC_CRSF_TICKS_MAX     1811U

/* ---- Framer ---- */

typedef enum {
	FC_CRSF_EVT_NONE = 0, /* byte consumed, no complete valid frame */
	FC_CRSF_EVT_FRAME,    /* a complete frame with a valid CRC is available */
} fc_crsf_event_t;

/* View of the last valid frame. Valid only until the next feed or reset call. */
typedef struct fc_crsf_frame {
	uint8_t sync;
	uint8_t type;
	const uint8_t *payload; /* excludes type and CRC */
	uint8_t payload_len;
} fc_crsf_frame_t;

typedef struct fc_crsf_stats {
	uint32_t frames_ok;
	uint32_t crc_errors;
	uint32_t len_errors;     /* length byte outside [LEN_MIN, LEN_MAX] */
	uint32_t garbage_bytes;  /* bytes skipped while hunting for sync */
} fc_crsf_stats_t;

/* Caller-owned. Treat all fields as private. */
typedef struct fc_crsf_parser {
	uint8_t state;
	uint8_t sync;
	uint8_t len;
	uint8_t idx;
	uint8_t buf[FC_CRSF_LEN_MAX];
	fc_crsf_stats_t stats;
} fc_crsf_parser_t;

/* CRC8, poly 0xD5, init 0. Over type + payload (not sync, not len). */
uint8_t fc_crsf_crc8(const uint8_t *data, size_t len);

/* Zero the context and the counters. NULL: no-op. */
void fc_crsf_parser_init(fc_crsf_parser_t *p);

/* Drop any partial frame and return to sync hunting. Counters are kept. NULL: no-op. */
void fc_crsf_parser_reset(fc_crsf_parser_t *p);

/*
 * Feed one received byte. Returns FC_CRSF_EVT_FRAME when this byte completed a valid
 * frame; *frame is then filled in. In every other case *frame is left untouched.
 * NULL p or frame: returns FC_CRSF_EVT_NONE and changes nothing.
 * Unknown frame types are valid frames; the caller decides what to do with them.
 */
fc_crsf_event_t fc_crsf_parser_feed(fc_crsf_parser_t *p, uint8_t byte, fc_crsf_frame_t *frame);

/* Counters. NULL p: returns NULL. */
const fc_crsf_stats_t *fc_crsf_parser_stats(const fc_crsf_parser_t *p);

/* ---- Decoders (stateless, raw wire values, no type check) ---- */

typedef struct fc_crsf_channels {
	uint16_t ticks[FC_CRSF_NUM_CHANNELS]; /* each 0..FC_CRSF_TICKS_RAW_MAX */
} fc_crsf_channels_t;

typedef struct fc_crsf_link_stats {
	uint8_t uplink_rssi_ant1;   /* dBm * -1 */
	uint8_t uplink_rssi_ant2;   /* dBm * -1 */
	uint8_t uplink_lq;          /* percent */
	int8_t uplink_snr;          /* dB */
	uint8_t active_antenna;
	uint8_t rf_profile;         /* raw enum index */
	uint8_t uplink_rf_power;    /* raw enum index */
	uint8_t downlink_rssi;      /* dBm * -1 */
	uint8_t downlink_lq;        /* percent */
	int8_t downlink_snr;        /* dB */
} fc_crsf_link_stats_t;

/*
 * Decode a 0x16 payload. Returns true on success. Returns false, leaving *out
 * untouched, if payload or out is NULL or len < FC_CRSF_CHANNELS_PAYLOAD_LEN.
 * Bytes beyond the minimum are ignored.
 */
bool fc_crsf_decode_channels(const uint8_t *payload, size_t len, fc_crsf_channels_t *out);

/* Same contract for a 0x14 payload with FC_CRSF_LINK_STATS_PAYLOAD_LEN. */
bool fc_crsf_decode_link_stats(const uint8_t *payload, size_t len, fc_crsf_link_stats_t *out);

/* ---- Conversion (separate from decoding) ---- */

/* Ticks to microseconds, using the spec's TICKS_TO_US. ticks above RAW_MAX are clamped. */
uint16_t fc_crsf_ticks_to_us(uint16_t ticks);

/* Ticks to [-1.0f, 1.0f], 0.0f at FC_CRSF_TICKS_CENTER, clamped at the ends. */
float fc_crsf_ticks_to_unit(uint16_t ticks);

#endif /* FC_CRSF_H_ */