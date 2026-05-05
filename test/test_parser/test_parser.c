/**
 * @file test_parser.c
 * @brief Unity test suite for uart_protocol — runs on host via PlatformIO native
 *
 * Coverage:
 *   - Valid frames (no data, with data)
 *   - Checksum mismatch
 *   - Buffer overflow (LEN too large)
 *   - Missing END marker
 *   - Incomplete frame timeout
 *   - Noise before START marker
 *   - Back-to-back valid frames
 *   - proto_encode round-trip
 */

#include <unity.h>
#include <stdlib.h>
#include "uart_protocol.h"

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

/**
 * @brief Feed a raw byte array into the parser, return the last error code.
 *        out_frame is filled only when PROTO_OK is returned.
 */
static proto_err_t feed(proto_parser_t *p,
						const uint8_t  *raw,
						uint8_t         len,
						proto_frame_t  *out)
{
	proto_err_t err = PROTO_INCOMPLETE;
	for (uint8_t i = 0; i < len; i++) {
		err = proto_parse_byte(p, raw[i], 0, out);
	}
	return err;
}

/**
 * @brief Build a valid raw frame for CMD_PING (no payload).
 *        START=0xAA CMD=0x01 LEN=0x00 CS=0x01 END=0x55
 */
static void make_ping(uint8_t *buf, uint8_t *out_len)
{
	proto_encode(CMD_PING, NULL, 0, buf, out_len);
}

/* ─── setUp / tearDown (called by Unity before/after each test) ───────────── */

void setUp(void)    {}
void tearDown(void) {}

/* ─── Test cases ──────────────────────────────────────────────────────────── */

/**
 * @brief A well-formed PING frame (no payload) must return PROTO_OK
 *        and populate cmd/len correctly.
 */
void test_valid_frame_no_data(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t buf[8];
	uint8_t len;
	make_ping(buf, &len);

	proto_err_t err = feed(&p, buf, len, &frame);

	TEST_ASSERT_EQUAL(PROTO_OK,  err);
	TEST_ASSERT_EQUAL(CMD_PING,  frame.cmd);
	TEST_ASSERT_EQUAL(0,         frame.len);
}

/**
 * @brief A frame carrying 3 data bytes must return PROTO_OK and
 *        preserve the payload exactly.
 */
void test_valid_frame_with_data(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t payload[3] = { 0x01, 0x02, 0x03 };
	uint8_t buf[16];
	uint8_t len;
	proto_encode(CMD_SET_LED, payload, sizeof(payload), buf, &len);

	proto_err_t err = feed(&p, buf, len, &frame);

	TEST_ASSERT_EQUAL(PROTO_OK,     err);
	TEST_ASSERT_EQUAL(CMD_SET_LED,  frame.cmd);
	TEST_ASSERT_EQUAL(3,            frame.len);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame.data, 3);
}

/**
 * @brief A corrupted checksum byte must return PROTO_ERR_CHECKSUM
 *        and leave the parser ready for the next frame.
 */
void test_bad_checksum(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t buf[8];
	uint8_t len;
	make_ping(buf, &len);
	buf[len - 2] ^= 0xFF;  /* Corrupt checksum byte */

	proto_err_t err = PROTO_INCOMPLETE;
	for (uint8_t i = 0; i < len; i++) {
		err = proto_parse_byte(&p, buf[i], 0, &frame);
		if (err != PROTO_INCOMPLETE) break;  /* Stop at first error */
	}

	TEST_ASSERT_EQUAL(PROTO_ERR_CHECKSUM, err);
	TEST_ASSERT_EQUAL(STATE_IDLE, p.state);
}

/**
 * @brief LEN > PROTO_MAX_DATA_LEN must return PROTO_ERR_OVERFLOW
 *        without writing out of bounds.
 */
void test_overflow(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	/* Craft a frame with LEN = 255 manually */
	uint8_t raw[] = { PROTO_START, CMD_SET_LED, 0xFF };

	proto_err_t err = feed(&p, raw, sizeof(raw), &frame);

	TEST_ASSERT_EQUAL(PROTO_ERR_OVERFLOW, err);
	TEST_ASSERT_EQUAL(STATE_IDLE, p.state);
}

/**
 * @brief A wrong END marker must return PROTO_ERR_BAD_END.
 */
void test_bad_end_marker(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t buf[8];
	uint8_t len;
	make_ping(buf, &len);

	buf[len - 1] = 0xFFU;  /* Corrupt END byte */

	proto_err_t err = feed(&p, buf, len, &frame);

	TEST_ASSERT_EQUAL(PROTO_ERR_BAD_END, err);
	TEST_ASSERT_EQUAL(STATE_IDLE, p.state);
}

/**
 * @brief Bytes received before START must be silently discarded.
 *        The frame after the noise must still parse correctly.
 */
void test_noise_before_start(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t buf[16];
	uint8_t len;
	make_ping(buf, &len);

	/* Prepend 4 noise bytes */
	uint8_t noisy[16];
	noisy[0] = 0x00;
	noisy[1] = 0xFF;
	noisy[2] = 0x12;
	noisy[3] = 0x34;
	for (uint8_t i = 0; i < len; i++) {
		noisy[4 + i] = buf[i];
	}

	proto_err_t err = feed(&p, noisy, 4 + len, &frame);

	TEST_ASSERT_EQUAL(PROTO_OK, err);
	TEST_ASSERT_EQUAL(CMD_PING, frame.cmd);
}

/**
 * @brief An incomplete frame followed by PROTO_TIMEOUT_MS elapsed
 *        must return PROTO_ERR_TIMEOUT and reset the parser.
 */
void test_timeout(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	/* Send only START + CMD — never complete the frame */
	uint8_t partial[] = { PROTO_START, CMD_PING };
	feed(&p, partial, sizeof(partial), &frame);

	/* Simulate time passing beyond the timeout threshold */
	proto_err_t err = proto_check_timeout(&p, PROTO_TIMEOUT_MS + 1U);

	TEST_ASSERT_EQUAL(PROTO_ERR_TIMEOUT, err);
	TEST_ASSERT_EQUAL(STATE_IDLE, p.state);
}

/**
 * @brief Two valid frames sent back-to-back must both parse correctly.
 *        Verifies that reset between frames is clean.
 */
void test_back_to_back_frames(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t buf[8];
	uint8_t len;
	make_ping(buf, &len);

	/* First frame */
	proto_err_t err = feed(&p, buf, len, &frame);
	TEST_ASSERT_EQUAL(PROTO_OK,  err);
	TEST_ASSERT_EQUAL(CMD_PING,  frame.cmd);

	/* Second frame — parser must have auto-reset */
	err = feed(&p, buf, len, &frame);
	TEST_ASSERT_EQUAL(PROTO_OK,  err);
	TEST_ASSERT_EQUAL(CMD_PING,  frame.cmd);
}

/**
 * @brief proto_encode followed by proto_parse_byte must reconstruct
 *        the original cmd and payload exactly (round-trip).
 */
void test_encode_decode_roundtrip(void)
{
	proto_parser_t p;
	proto_frame_t  frame;
	proto_parser_init(&p);

	uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t buf[16];
	uint8_t len;

	bool ok = proto_encode(CMD_GET_IMU, payload, sizeof(payload), buf, &len);
	TEST_ASSERT_TRUE(ok);

	proto_err_t err = feed(&p, buf, len, &frame);

	TEST_ASSERT_EQUAL(PROTO_OK,     err);
	TEST_ASSERT_EQUAL(CMD_GET_IMU,  frame.cmd);
	TEST_ASSERT_EQUAL(4,            frame.len);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame.data, 4);
}

/**
 * @brief proto_encode must reject payloads larger than PROTO_MAX_DATA_LEN.
 */
void test_encode_overflow(void)
{
	uint8_t buf[300];
	uint8_t len;
	uint8_t big[PROTO_MAX_DATA_LEN + 1U];

	bool ok = proto_encode(CMD_SET_LED, big, sizeof(big), buf, &len);
	TEST_ASSERT_FALSE(ok);
}

/* ─── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_valid_frame_no_data);
	RUN_TEST(test_valid_frame_with_data);
	RUN_TEST(test_bad_checksum);
	RUN_TEST(test_overflow);
	RUN_TEST(test_bad_end_marker);
	RUN_TEST(test_noise_before_start);
	RUN_TEST(test_timeout);
	RUN_TEST(test_back_to_back_frames);
	RUN_TEST(test_encode_decode_roundtrip);
	RUN_TEST(test_encode_overflow);

	UNITY_END();
	exit(0);
}