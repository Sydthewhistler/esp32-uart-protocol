/**
 * @file uart_protocol.h
 * @brief Custom UART framing protocol — state-machine frame parser
 *
 * Frame format:
 *   [START 0xAA][CMD 1B][LEN 1B][DATA 0-255B][CHECKSUM 1B][END 0x55]
 *
 * CHECKSUM = XOR(CMD, LEN, DATA[0..LEN-1])
 *
 * @author  Sydney Cavallin
 * @date    03-05-2026
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Frame constants ─────────────────────────────────────────────────────── */

#define PROTO_START			0xAAU   /**< Start-of-frame marker             */
#define PROTO_END			0x55U   /**< End-of-frame marker               */
#define PROTO_MAX_DATA_LEN	64U    /**< Maximum DATA field length (bytes) */
#define PROTO_TIMEOUT_MS	100U    /**< Incomplete frame timeout (ms)     */

/* ─── Command codes ───────────────────────────────────────────────────────── */

#define CMD_PING		0x01U   /**< Ping — empty DATA, expects PONG     */
#define CMD_PONG		0x02U   /**< Pong — reply to PING                */
#define CMD_SET_LED		0x10U   /**< Set LED state, DATA[0]=0/1          */
#define CMD_GET_IMU		0x20U   /**< Request IMU data                    */
#define CMD_IMU_DATA	0x21U   /**< IMU data response (6 x int16_t)     */
#define CMD_ACK			0xF0U   /**< Generic acknowledgement             */
#define CMD_NACK		0xF1U   /**< Negative acknowledgement + err code */

/* ─── Error codes ─────────────────────────────────────────────────────────── */

typedef enum {
	PROTO_OK			= 0,   /**< Frame received and valid            */
	PROTO_ERR_TIMEOUT	= 1,   /**< Incomplete frame, timeout expired   */
	PROTO_ERR_CHECKSUM	= 2,   /**< Checksum mismatch                   */
	PROTO_ERR_OVERFLOW	= 3,   /**< LEN exceeds PROTO_MAX_DATA_LEN      */
	PROTO_ERR_BAD_END	= 4,   /**< End marker missing or incorrect     */
	PROTO_INCOMPLETE	= 5,   /**< Frame reception in progress         */
} proto_err_t;

/* ─── Decoded frame structure ─────────────────────────────────────────────── */

typedef struct {
	uint8_t cmd;
	uint8_t len;
	uint8_t data[PROTO_MAX_DATA_LEN];
} proto_frame_t;

/* ─── Parser internal states (opaque to the caller) ──────────────────────── */

typedef enum {
	STATE_IDLE,
	STATE_CMD,
	STATE_LEN,
	STATE_DATA,
	STATE_CHECKSUM,
	STATE_END,
} parser_state_t;

/* ─── Parser context (one instance per UART channel) ─────────────────────── */

typedef struct {
	parser_state_t	state;
	proto_frame_t	frame;
	uint8_t			data_idx;
	uint8_t			checksum_calc;
	uint32_t		last_byte_ms;
} proto_parser_t;

/* ─── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialize a parser context.
 * @param p  Pointer to the parser context.
 */
void proto_parser_init(proto_parser_t *p);

/**
 * @brief Reset the parser to IDLE state.
 *        Called on error or after a complete frame has been consumed.
 * @param p  Pointer to the parser context.
 */
void proto_parser_reset(proto_parser_t *p);

/**
 * @brief Feed one received byte into the parser.
 *        Non-blocking — must be called from the main loop.
 *
 * @param p          Parser context.
 * @param byte       Received byte.
 * @param now_ms     Current timestamp from millis().
 * @param out_frame  Output frame filled when PROTO_OK is returned.
 * @return           PROTO_OK if a complete valid frame was received,
 *                   PROTO_INCOMPLETE if reception is still in progress,
 *                   error code otherwise.
 */
proto_err_t proto_parse_byte(proto_parser_t *p,
							 uint8_t         byte,
							 uint32_t        now_ms,
							 proto_frame_t  *out_frame);

/**
 * @brief Check for an incomplete frame timeout.
 *        Must be called periodically from the main loop.
 *
 * @param p       Parser context.
 * @param now_ms  Current timestamp from millis().
 * @return        PROTO_ERR_TIMEOUT if timeout expired and parser was reset,
 *                PROTO_INCOMPLETE otherwise.
 */
proto_err_t proto_check_timeout(proto_parser_t *p, uint32_t now_ms);

/**
 * @brief Encode a frame into an output buffer.
 *
 * @param cmd      Command byte.
 * @param data     Payload data (may be NULL if len == 0).
 * @param len      Payload length in bytes.
 * @param out_buf  Output buffer (must be at least len + 5 bytes).
 * @param out_len  Encoded frame length written here.
 * @return         true on success, false if len > PROTO_MAX_DATA_LEN.
 */
bool proto_encode(uint8_t        cmd,
				  const uint8_t *data,
				  uint8_t        len,
				  uint8_t       *out_buf,
				  uint8_t       *out_len);

#endif /* UART_PROTOCOL_H */
