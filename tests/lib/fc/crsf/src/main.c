/*
 * Unit tests for lib/fc crsf: CRC, framer, decoders, conversion.
 *
 * Independent references are used on purpose: a bitwise CRC (the library uses a table)
 * and a bit-by-bit channel packer (the library uses a bit accumulator). Several vectors
 * are written out by hand so that a shared misunderstanding cannot hide in both sides.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include <fc/crsf.h>

/* ---- Test helpers ---- */

/* Bitwise CRC8, poly 0xD5, init 0; independent of the library's table. */
static uint8_t ref_crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0U;

	for (size_t i = 0U; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x80U) != 0U) {
				crc = (uint8_t)((uint32_t)(crc << 1) ^ 0xD5U);
			} else {
				crc = (uint8_t)(crc << 1);
			}
		}
	}

	return crc;
}

/* Bit-by-bit packer: channel i occupies bits [11 i, 11 i + 10], LSB first. */
static void ref_pack_channels(const uint16_t ch[16], uint8_t out[22])
{
	memset(out, 0, 22U);

	for (unsigned int i = 0U; i < 16U; i++) {
		for (unsigned int b = 0U; b < 11U; b++) {
			if (((ch[i] >> b) & 1U) != 0U) {
				unsigned int bit = i * 11U + b;

				out[bit / 8U] = (uint8_t)(out[bit / 8U] | (1U << (bit % 8U)));
			}
		}
	}
}

/* Build a complete frame. Returns the total size in bytes. */
static size_t build_frame(uint8_t *out, uint8_t sync, uint8_t type, const uint8_t *payload,
			  size_t plen)
{
	out[0] = sync;
	out[1] = (uint8_t)(plen + 2U);
	out[2] = type;
	if (plen > 0U) {
		memcpy(&out[3], payload, plen);
	}
	out[3U + plen] = ref_crc8(&out[2], plen + 1U);

	return plen + 4U;
}

/* What the tests learn about frames seen while feeding. */
struct capture {
	unsigned int frames;
	uint8_t sync;
	uint8_t type;
	uint8_t payload_len;
	uint8_t payload[FC_CRSF_LEN_MAX];
};

static void feed_bytes(fc_crsf_parser_t *p, const uint8_t *data, size_t len, struct capture *cap)
{
	fc_crsf_frame_t frame;

	for (size_t i = 0U; i < len; i++) {
		if (fc_crsf_parser_feed(p, data[i], &frame) == FC_CRSF_EVT_FRAME) {
			cap->frames++;
			cap->sync = frame.sync;
			cap->type = frame.type;
			cap->payload_len = frame.payload_len;
			memcpy(cap->payload, frame.payload, frame.payload_len);
		}
	}
}

/* Hand-written RC frame with every channel at 992: the usual "sticks centred" frame. */
static const uint8_t centred_payload[22] = {0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0,
					    0x81, 0x0F, 0x7C, 0xE0, 0x03, 0x1F, 0xF8, 0xC0,
					    0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C};

/* ---- CRC ---- */

ZTEST_SUITE(crsf_crc, NULL, NULL, NULL, NULL, NULL);

ZTEST(crsf_crc, test_empty_and_null)
{
	uint8_t byte = 0x55U;

	zassert_equal(fc_crsf_crc8(&byte, 0U), 0U);
	zassert_equal(fc_crsf_crc8(NULL, 5U), 0U);
}

ZTEST(crsf_crc, test_single_byte_is_table_entry)
{
	/* With init 0, crc8({b}) is the polynomial remainder of b: 0x01 gives the poly. */
	uint8_t one = 0x01U;
	uint8_t two = 0x02U;

	zassert_equal(fc_crsf_crc8(&one, 1U), 0xD5U);
	zassert_equal(fc_crsf_crc8(&two, 1U), 0x7FU);
}

ZTEST(crsf_crc, test_matches_bitwise_reference_for_every_single_byte)
{
	for (unsigned int b = 0U; b < 256U; b++) {
		uint8_t byte = (uint8_t)b;

		zassert_equal(fc_crsf_crc8(&byte, 1U), ref_crc8(&byte, 1U));
	}
}

ZTEST(crsf_crc, test_matches_bitwise_reference_on_pseudo_random_buffers)
{
	uint8_t buf[64];
	uint32_t x = 0x12345678U;

	for (size_t len = 1U; len <= sizeof(buf); len++) {
		for (size_t i = 0U; i < len; i++) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			buf[i] = (uint8_t)x;
		}
		zassert_equal(fc_crsf_crc8(buf, len), ref_crc8(buf, len));
	}
}

ZTEST(crsf_crc, test_centred_rc_frame_crc)
{
	uint8_t data[23];

	data[0] = FC_CRSF_TYPE_RC_CHANNELS;
	memcpy(&data[1], centred_payload, sizeof(centred_payload));

	zassert_equal(fc_crsf_crc8(data, sizeof(data)), 0xADU);
}

/* ---- Framer ---- */

ZTEST_SUITE(crsf_framer, NULL, NULL, NULL, NULL, NULL);

ZTEST(crsf_framer, test_init_zeroes_everything)
{
	fc_crsf_parser_t p;
	const fc_crsf_stats_t *s;

	memset(&p, 0xAA, sizeof(p));
	fc_crsf_parser_init(&p);
	s = fc_crsf_parser_stats(&p);

	zassert_not_null(s);
	zassert_equal(s->frames_ok, 0U);
	zassert_equal(s->crc_errors, 0U);
	zassert_equal(s->len_errors, 0U);
	zassert_equal(s->garbage_bytes, 0U);
}

ZTEST(crsf_framer, test_single_valid_frame)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	n = build_frame(frame, FC_CRSF_SYNC_FC, FC_CRSF_TYPE_RC_CHANNELS, centred_payload,
			sizeof(centred_payload));
	zassert_equal(n, 26U);

	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(cap.sync, FC_CRSF_SYNC_FC);
	zassert_equal(cap.type, FC_CRSF_TYPE_RC_CHANNELS);
	zassert_equal(cap.payload_len, 22U);
	zassert_mem_equal(cap.payload, centred_payload, 22U);
	zassert_equal(fc_crsf_parser_stats(&p)->frames_ok, 1U);
}

ZTEST(crsf_framer, test_hand_written_wire_frame)
{
	/* Literal bytes, no helper involved. */
	static const uint8_t wire[] = {0xC8, 0x18, 0x16, 0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07,
				       0x3E, 0xF0, 0x81, 0x0F, 0x7C, 0xE0, 0x03, 0x1F, 0xF8,
				       0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C, 0xAD};
	fc_crsf_parser_t p;
	struct capture cap;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	feed_bytes(&p, wire, sizeof(wire), &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(cap.type, 0x16U);
	zassert_mem_equal(cap.payload, centred_payload, 22U);
}

ZTEST(crsf_framer, test_event_only_on_last_byte)
{
	fc_crsf_parser_t p;
	fc_crsf_frame_t f;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x14U, centred_payload, 10U);

	fc_crsf_parser_init(&p);
	for (size_t i = 0U; i + 1U < n; i++) {
		zassert_equal(fc_crsf_parser_feed(&p, frame[i], &f), FC_CRSF_EVT_NONE);
	}
	zassert_equal(fc_crsf_parser_feed(&p, frame[n - 1U], &f), FC_CRSF_EVT_FRAME);
}

ZTEST(crsf_framer, test_back_to_back_frames_of_different_types)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t stream[3 * FC_CRSF_MAX_FRAME_SIZE];
	uint8_t link[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	size_t n = 0U;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x14U, link, sizeof(link));
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	feed_bytes(&p, stream, n, &cap);

	zassert_equal(cap.frames, 3U);
	zassert_equal(cap.type, 0x16U);
	zassert_equal(fc_crsf_parser_stats(&p)->frames_ok, 3U);
	zassert_equal(fc_crsf_parser_stats(&p)->garbage_bytes, 0U);
}

ZTEST(crsf_framer, test_frame_split_at_every_position)
{
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	for (size_t split = 1U; split < n; split++) {
		fc_crsf_parser_t p;
		struct capture cap;

		memset(&cap, 0, sizeof(cap));
		fc_crsf_parser_init(&p);
		feed_bytes(&p, frame, split, &cap);
		zassert_equal(cap.frames, 0U);
		feed_bytes(&p, &frame[split], n - split, &cap);
		zassert_equal(cap.frames, 1U);
	}
}

ZTEST(crsf_framer, test_garbage_before_sync_is_skipped_and_counted)
{
	static const uint8_t garbage[] = {0x00, 0x01, 0xFF, 0x7E, 0x55};
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	feed_bytes(&p, garbage, sizeof(garbage), &cap);
	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(fc_crsf_parser_stats(&p)->garbage_bytes, 5U);
}

ZTEST(crsf_framer, test_garbage_between_frames)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t stream[2 * FC_CRSF_MAX_FRAME_SIZE + 3];
	size_t n = 0U;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);
	stream[n++] = 0x01U;
	stream[n++] = 0x02U;
	stream[n++] = 0x03U;
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	feed_bytes(&p, stream, n, &cap);

	zassert_equal(cap.frames, 2U);
	zassert_equal(fc_crsf_parser_stats(&p)->garbage_bytes, 3U);
}

ZTEST(crsf_framer, test_length_below_minimum_is_rejected)
{
	for (uint8_t bad = 0U; bad < FC_CRSF_LEN_MIN; bad++) {
		fc_crsf_parser_t p;
		struct capture cap;
		uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
		size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

		memset(&cap, 0, sizeof(cap));
		fc_crsf_parser_init(&p);
		feed_bytes(&p, (const uint8_t[]){FC_CRSF_SYNC_FC, bad}, 2U, &cap);
		zassert_equal(fc_crsf_parser_stats(&p)->len_errors, 1U);

		feed_bytes(&p, frame, n, &cap);
		zassert_equal(cap.frames, 1U);
	}
}

ZTEST(crsf_framer, test_length_above_maximum_is_rejected)
{
	static const uint8_t bad_lengths[] = {63U, 64U, 100U, 255U};

	for (size_t i = 0U; i < sizeof(bad_lengths); i++) {
		fc_crsf_parser_t p;
		struct capture cap;
		uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
		size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

		memset(&cap, 0, sizeof(cap));
		fc_crsf_parser_init(&p);
		feed_bytes(&p, (const uint8_t[]){FC_CRSF_SYNC_FC, bad_lengths[i]}, 2U, &cap);
		zassert_equal(fc_crsf_parser_stats(&p)->len_errors, 1U);

		feed_bytes(&p, frame, n, &cap);
		zassert_equal(cap.frames, 1U);
	}
}

ZTEST(crsf_framer, test_rejected_length_byte_that_is_a_sync_byte_starts_the_next_frame)
{
	/* A stray sync byte, then a complete valid frame: the frame's own sync byte is
	 * first seen as an out-of-range length (0xC8 = 200) and must not be lost.
	 */
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t stream[1 + FC_CRSF_MAX_FRAME_SIZE];
	size_t n = 0U;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	stream[n++] = FC_CRSF_SYNC_FC;
	n += build_frame(&stream[n], FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	feed_bytes(&p, stream, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(fc_crsf_parser_stats(&p)->len_errors, 1U);
}

ZTEST(crsf_framer, test_corrupted_payload_is_dropped_then_next_frame_is_accepted)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t bad[FC_CRSF_MAX_FRAME_SIZE];
	uint8_t good[FC_CRSF_MAX_FRAME_SIZE];
	size_t n_bad = build_frame(bad, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);
	size_t n_good = build_frame(good, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	bad[10] ^= 0x01U;

	feed_bytes(&p, bad, n_bad, &cap);
	zassert_equal(cap.frames, 0U);
	zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 1U);

	feed_bytes(&p, good, n_good, &cap);
	zassert_equal(cap.frames, 1U);
	zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 1U);
}

ZTEST(crsf_framer, test_corrupted_crc_byte_is_dropped)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	frame[n - 1U] ^= 0xFFU;

	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 0U);
	zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 1U);
}

ZTEST(crsf_framer, test_corrupted_type_byte_is_dropped)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	frame[2] ^= 0x01U;

	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 0U);
	zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 1U);
}

ZTEST(crsf_framer, test_maximum_length_frame)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t payload[60];
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n;

	for (size_t i = 0U; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)(i * 3U + 1U);
	}
	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	n = build_frame(frame, FC_CRSF_SYNC_FC, 0x7AU, payload, sizeof(payload));
	zassert_equal(n, FC_CRSF_MAX_FRAME_SIZE);
	zassert_equal(frame[1], FC_CRSF_LEN_MAX);

	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(cap.payload_len, 60U);
	zassert_mem_equal(cap.payload, payload, sizeof(payload));
}

ZTEST(crsf_framer, test_minimum_length_frame_has_empty_payload)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n;

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);
	n = build_frame(frame, FC_CRSF_SYNC_FC, 0x28U, NULL, 0U);
	zassert_equal(n, 4U);
	zassert_equal(frame[1], FC_CRSF_LEN_MIN);

	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(cap.type, 0x28U);
	zassert_equal(cap.payload_len, 0U);
}

ZTEST(crsf_framer, test_unknown_types_are_valid_frames)
{
	static const uint8_t types[] = {0x00U, 0x02U, 0x08U, 0x21U, 0x7AU, 0xAAU, 0xFFU};

	for (size_t i = 0U; i < sizeof(types); i++) {
		fc_crsf_parser_t p;
		struct capture cap;
		uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
		size_t n = build_frame(frame, FC_CRSF_SYNC_FC, types[i], centred_payload, 5U);

		memset(&cap, 0, sizeof(cap));
		fc_crsf_parser_init(&p);
		feed_bytes(&p, frame, n, &cap);

		zassert_equal(cap.frames, 1U);
		zassert_equal(cap.type, types[i]);
		zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 0U);
	}
}

ZTEST(crsf_framer, test_other_sync_bytes_are_not_accepted)
{
	/* Policy C7: only 0xC8 starts a frame. Pick a payload without a 0xC8 byte. */
	static const uint8_t other_syncs[] = {0x00U, 0xEAU, 0xEEU, 0xECU};

	for (size_t i = 0U; i < sizeof(other_syncs); i++) {
		fc_crsf_parser_t p;
		struct capture cap;
		uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
		size_t n = build_frame(frame, other_syncs[i], 0x16U, centred_payload, 22U);

		memset(&cap, 0, sizeof(cap));
		fc_crsf_parser_init(&p);
		feed_bytes(&p, frame, n, &cap);

		zassert_equal(cap.frames, 0U);
	}
}

ZTEST(crsf_framer, test_lost_byte_costs_the_next_frame_too)
{
	/* Characterises policy C7: after a CRC failure the framer hunts from the next
	 * byte, so a frame swallowed by a lost byte is lost as well. The third frame is
	 * recovered.
	 */
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t stream[3 * FC_CRSF_MAX_FRAME_SIZE];
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = 0U;
	size_t len = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);

	/* Frame A without its byte at index 10, then frames B and C intact. */
	memcpy(&stream[n], frame, 10U);
	n += 10U;
	memcpy(&stream[n], &frame[11], len - 11U);
	n += len - 11U;
	memcpy(&stream[n], frame, len);
	n += len;
	memcpy(&stream[n], frame, len);
	n += len;

	feed_bytes(&p, stream, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(fc_crsf_parser_stats(&p)->crc_errors, 1U);
}

ZTEST(crsf_framer, test_reset_drops_a_partial_frame_and_keeps_counters)
{
	fc_crsf_parser_t p;
	struct capture cap;
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	size_t n = build_frame(frame, FC_CRSF_SYNC_FC, 0x16U, centred_payload, 22U);

	memset(&cap, 0, sizeof(cap));
	fc_crsf_parser_init(&p);

	feed_bytes(&p, (const uint8_t[]){0x01, 0x02}, 2U, &cap); /* two garbage bytes */
	feed_bytes(&p, frame, 12U, &cap);                        /* half a frame */
	fc_crsf_parser_reset(&p);
	feed_bytes(&p, frame, n, &cap);

	zassert_equal(cap.frames, 1U);
	zassert_equal(fc_crsf_parser_stats(&p)->garbage_bytes, 2U);
	zassert_equal(fc_crsf_parser_stats(&p)->frames_ok, 1U);
}

ZTEST(crsf_framer, test_frame_view_is_untouched_when_no_frame_completes)
{
	fc_crsf_parser_t p;
	fc_crsf_frame_t f;
	uint8_t byte = 0x42U;

	fc_crsf_parser_init(&p);
	f.sync = 0x11U;
	f.type = 0x22U;
	f.payload = NULL;
	f.payload_len = 0x33U;

	zassert_equal(fc_crsf_parser_feed(&p, byte, &f), FC_CRSF_EVT_NONE);
	zassert_equal(f.sync, 0x11U);
	zassert_equal(f.type, 0x22U);
	zassert_is_null(f.payload);
	zassert_equal(f.payload_len, 0x33U);
}

ZTEST(crsf_framer, test_null_arguments)
{
	fc_crsf_parser_t p;
	fc_crsf_frame_t f;

	fc_crsf_parser_init(&p);

	zassert_equal(fc_crsf_parser_feed(NULL, 0xC8U, &f), FC_CRSF_EVT_NONE);
	zassert_equal(fc_crsf_parser_feed(&p, 0xC8U, NULL), FC_CRSF_EVT_NONE);
	zassert_is_null(fc_crsf_parser_stats(NULL));

	/* The rejected call must not have advanced the state machine. */
	zassert_equal(fc_crsf_parser_stats(&p)->garbage_bytes, 0U);

	fc_crsf_parser_init(NULL);
	fc_crsf_parser_reset(NULL);
}

/* ---- Channel decoder ---- */

ZTEST_SUITE(crsf_channels, NULL, NULL, NULL, NULL, NULL);

ZTEST(crsf_channels, test_hand_vector_channel_1_only)
{
	uint8_t payload[22] = {0xFF, 0x07};
	fc_crsf_channels_t ch;

	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	zassert_equal(ch.ticks[0], 2047U);
	for (size_t i = 1U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], 0U);
	}
}

ZTEST(crsf_channels, test_hand_vector_channel_2_only)
{
	uint8_t payload[22] = {0x00, 0xF8, 0x3F};
	fc_crsf_channels_t ch;

	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	zassert_equal(ch.ticks[0], 0U);
	zassert_equal(ch.ticks[1], 2047U);
	for (size_t i = 2U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], 0U);
	}
}

ZTEST(crsf_channels, test_hand_vector_channel_16_only)
{
	uint8_t payload[22] = {0};
	fc_crsf_channels_t ch;

	payload[20] = 0xE0U;
	payload[21] = 0xFFU;

	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	for (size_t i = 0U; i < 15U; i++) {
		zassert_equal(ch.ticks[i], 0U);
	}
	zassert_equal(ch.ticks[15], 2047U);
}

ZTEST(crsf_channels, test_hand_vector_all_centred)
{
	fc_crsf_channels_t ch;

	zassert_true(fc_crsf_decode_channels(centred_payload, sizeof(centred_payload), &ch));
	for (size_t i = 0U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], 992U);
	}
}

ZTEST(crsf_channels, test_all_zero_and_all_ones)
{
	uint8_t payload[22];
	fc_crsf_channels_t ch;

	memset(payload, 0x00, sizeof(payload));
	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	for (size_t i = 0U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], 0U);
	}

	memset(payload, 0xFF, sizeof(payload));
	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	for (size_t i = 0U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], 2047U);
	}
}

ZTEST(crsf_channels, test_one_channel_at_a_time_has_no_neighbour_bleed)
{
	for (size_t hot = 0U; hot < 16U; hot++) {
		uint16_t in[16] = {0};
		uint8_t payload[22];
		fc_crsf_channels_t ch;

		in[hot] = 2047U;
		ref_pack_channels(in, payload);

		zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
		for (size_t i = 0U; i < 16U; i++) {
			zassert_equal(ch.ticks[i], in[i]);
		}
	}
}

ZTEST(crsf_channels, test_alternating_bit_patterns)
{
	static const uint16_t patterns[] = {0x2AAU, 0x555U, 0x7FFU, 0x400U, 0x001U};

	for (size_t k = 0U; k < sizeof(patterns) / sizeof(patterns[0]); k++) {
		uint16_t in[16];
		uint8_t payload[22];
		fc_crsf_channels_t ch;

		for (size_t i = 0U; i < 16U; i++) {
			in[i] = (i % 2U == 0U) ? patterns[k] : (uint16_t)(~patterns[k] & 0x7FFU);
		}
		ref_pack_channels(in, payload);

		zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
		for (size_t i = 0U; i < 16U; i++) {
			zassert_equal(ch.ticks[i], in[i]);
		}
	}
}

ZTEST(crsf_channels, test_distinct_values_round_trip)
{
	uint16_t in[16];
	uint8_t payload[22];
	fc_crsf_channels_t ch;

	for (size_t i = 0U; i < 16U; i++) {
		in[i] = (uint16_t)((i * 127U + 13U) & 0x7FFU);
	}
	ref_pack_channels(in, payload);

	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
	for (size_t i = 0U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], in[i]);
	}
}

ZTEST(crsf_channels, test_every_value_of_every_channel_round_trips)
{
	for (size_t c = 0U; c < 16U; c++) {
		for (uint16_t v = 0U; v <= 2047U; v++) {
			uint16_t in[16] = {0};
			uint8_t payload[22];
			fc_crsf_channels_t ch;

			in[c] = v;
			ref_pack_channels(in, payload);
			zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &ch));
			zassert_equal(ch.ticks[c], v);
		}
	}
}

ZTEST(crsf_channels, test_decode_a_frame_that_came_through_the_framer)
{
	uint16_t in[16];
	uint8_t payload[22];
	uint8_t frame[FC_CRSF_MAX_FRAME_SIZE];
	fc_crsf_parser_t p;
	fc_crsf_frame_t f;
	fc_crsf_channels_t ch;
	fc_crsf_event_t ev = FC_CRSF_EVT_NONE;
	size_t n;

	for (size_t i = 0U; i < 16U; i++) {
		in[i] = (uint16_t)(172U + i * 100U);
	}
	ref_pack_channels(in, payload);
	n = build_frame(frame, FC_CRSF_SYNC_FC, FC_CRSF_TYPE_RC_CHANNELS, payload, sizeof(payload));

	fc_crsf_parser_init(&p);
	for (size_t i = 0U; i < n; i++) {
		ev = fc_crsf_parser_feed(&p, frame[i], &f);
	}

	zassert_equal(ev, FC_CRSF_EVT_FRAME);
	zassert_equal(f.type, FC_CRSF_TYPE_RC_CHANNELS);
	zassert_true(fc_crsf_decode_channels(f.payload, f.payload_len, &ch));
	for (size_t i = 0U; i < 16U; i++) {
		zassert_equal(ch.ticks[i], in[i]);
	}
}

ZTEST(crsf_channels, test_longer_payload_is_accepted_and_extra_bytes_ignored)
{
	uint8_t payload[40];
	fc_crsf_channels_t a;
	fc_crsf_channels_t b;

	memcpy(payload, centred_payload, 22U);
	memset(&payload[22], 0x00, sizeof(payload) - 22U);
	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &a));

	memset(&payload[22], 0xFF, sizeof(payload) - 22U);
	zassert_true(fc_crsf_decode_channels(payload, sizeof(payload), &b));

	zassert_mem_equal(&a, &b, sizeof(a));
	zassert_equal(a.ticks[15], 992U);
}

ZTEST(crsf_channels, test_short_payload_is_rejected_and_output_untouched)
{
	fc_crsf_channels_t ch;
	fc_crsf_channels_t sentinel;

	memset(&ch, 0xA5, sizeof(ch));
	memset(&sentinel, 0xA5, sizeof(sentinel));

	zassert_false(fc_crsf_decode_channels(centred_payload, 21U, &ch));
	zassert_false(fc_crsf_decode_channels(centred_payload, 0U, &ch));
	zassert_mem_equal(&ch, &sentinel, sizeof(ch));
}

ZTEST(crsf_channels, test_null_arguments)
{
	fc_crsf_channels_t ch;
	fc_crsf_channels_t sentinel;

	memset(&ch, 0xA5, sizeof(ch));
	memset(&sentinel, 0xA5, sizeof(sentinel));

	zassert_false(fc_crsf_decode_channels(NULL, 22U, &ch));
	zassert_false(fc_crsf_decode_channels(centred_payload, 22U, NULL));
	zassert_mem_equal(&ch, &sentinel, sizeof(ch));
}

/* ---- Link statistics decoder ---- */

ZTEST_SUITE(crsf_link_stats, NULL, NULL, NULL, NULL, NULL);

ZTEST(crsf_link_stats, test_every_field_maps_to_its_own_byte)
{
	static const uint8_t payload[10] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
	fc_crsf_link_stats_t s;

	zassert_true(fc_crsf_decode_link_stats(payload, sizeof(payload), &s));

	zassert_equal(s.uplink_rssi_ant1, 10U);
	zassert_equal(s.uplink_rssi_ant2, 20U);
	zassert_equal(s.uplink_lq, 30U);
	zassert_equal(s.uplink_snr, 40);
	zassert_equal(s.active_antenna, 50U);
	zassert_equal(s.rf_profile, 60U);
	zassert_equal(s.uplink_rf_power, 70U);
	zassert_equal(s.downlink_rssi, 80U);
	zassert_equal(s.downlink_lq, 90U);
	zassert_equal(s.downlink_snr, 100);
}

ZTEST(crsf_link_stats, test_snr_is_signed)
{
	uint8_t payload[10] = {0};
	fc_crsf_link_stats_t s;

	payload[3] = 0xFBU; /* -5 */
	payload[9] = 0x80U; /* -128 */
	zassert_true(fc_crsf_decode_link_stats(payload, sizeof(payload), &s));
	zassert_equal(s.uplink_snr, -5);
	zassert_equal(s.downlink_snr, -128);

	payload[3] = 0x7FU; /* 127 */
	payload[9] = 0xFFU; /* -1 */
	zassert_true(fc_crsf_decode_link_stats(payload, sizeof(payload), &s));
	zassert_equal(s.uplink_snr, 127);
	zassert_equal(s.downlink_snr, -1);
}

ZTEST(crsf_link_stats, test_unsigned_fields_keep_their_full_range)
{
	uint8_t payload[10];
	fc_crsf_link_stats_t s;

	memset(payload, 0xFF, sizeof(payload));
	zassert_true(fc_crsf_decode_link_stats(payload, sizeof(payload), &s));

	zassert_equal(s.uplink_rssi_ant1, 255U);
	zassert_equal(s.uplink_lq, 255U);
	zassert_equal(s.downlink_rssi, 255U);
	zassert_equal(s.downlink_lq, 255U);
}

ZTEST(crsf_link_stats, test_longer_payload_is_accepted)
{
	uint8_t payload[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
	fc_crsf_link_stats_t s;

	zassert_true(fc_crsf_decode_link_stats(payload, sizeof(payload), &s));
	zassert_equal(s.downlink_snr, 10);
}

ZTEST(crsf_link_stats, test_short_payload_is_rejected_and_output_untouched)
{
	static const uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	fc_crsf_link_stats_t s;
	fc_crsf_link_stats_t sentinel;

	memset(&s, 0xA5, sizeof(s));
	memset(&sentinel, 0xA5, sizeof(sentinel));

	zassert_false(fc_crsf_decode_link_stats(payload, 9U, &s));
	zassert_false(fc_crsf_decode_link_stats(payload, 0U, &s));
	zassert_mem_equal(&s, &sentinel, sizeof(s));
}

ZTEST(crsf_link_stats, test_null_arguments)
{
	static const uint8_t payload[10] = {0};
	fc_crsf_link_stats_t s;

	zassert_false(fc_crsf_decode_link_stats(NULL, 10U, &s));
	zassert_false(fc_crsf_decode_link_stats(payload, 10U, NULL));
}

/* ---- Conversion ---- */

ZTEST_SUITE(crsf_convert, NULL, NULL, NULL, NULL, NULL);

ZTEST(crsf_convert, test_ticks_to_us_known_points)
{
	zassert_equal(fc_crsf_ticks_to_us(992U), 1500U);
	zassert_equal(fc_crsf_ticks_to_us(172U), 988U);
	zassert_equal(fc_crsf_ticks_to_us(1811U), 2011U);
	zassert_equal(fc_crsf_ticks_to_us(0U), 880U);
	zassert_equal(fc_crsf_ticks_to_us(2047U), 2159U);
}

ZTEST(crsf_convert, test_ticks_to_us_clamps_above_raw_range)
{
	zassert_equal(fc_crsf_ticks_to_us(2048U), 2159U);
	zassert_equal(fc_crsf_ticks_to_us(65535U), 2159U);
}

ZTEST(crsf_convert, test_ticks_to_us_matches_spec_formula_and_never_decreases)
{
	uint16_t previous = 0U;

	for (int32_t t = 0; t <= 2047; t++) {
		uint16_t us = fc_crsf_ticks_to_us((uint16_t)t);

		zassert_equal((int32_t)us, ((t - 992) * 5) / 8 + 1500);
		zassert_true(us >= previous);
		previous = us;
	}
}

ZTEST(crsf_convert, test_ticks_to_unit_known_points)
{
	zassert_equal(fc_crsf_ticks_to_unit(992U), 0.0f);
	zassert_equal(fc_crsf_ticks_to_unit(1811U), 1.0f);
	zassert_equal(fc_crsf_ticks_to_unit(172U), -1.0f);
	zassert_within(fc_crsf_ticks_to_unit(1401U), 0.4994f, 0.0001f);
	zassert_within(fc_crsf_ticks_to_unit(582U), -0.5f, 0.0001f);
}

ZTEST(crsf_convert, test_ticks_to_unit_clamps_beyond_the_end_points)
{
	zassert_equal(fc_crsf_ticks_to_unit(0U), -1.0f);
	zassert_equal(fc_crsf_ticks_to_unit(171U), -1.0f);
	zassert_equal(fc_crsf_ticks_to_unit(1812U), 1.0f);
	zassert_equal(fc_crsf_ticks_to_unit(2047U), 1.0f);
	zassert_equal(fc_crsf_ticks_to_unit(65535U), 1.0f);
}

ZTEST(crsf_convert, test_ticks_to_unit_is_monotonic_and_bounded)
{
	float previous = -2.0f;

	for (uint16_t t = 0U; t <= 2047U; t++) {
		float u = fc_crsf_ticks_to_unit(t);

		zassert_true(u >= -1.0f);
		zassert_true(u <= 1.0f);
		zassert_true(u >= previous);
		previous = u;
	}
}