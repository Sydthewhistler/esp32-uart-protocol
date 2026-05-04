#include <Arduino.h>

extern "C" {
	#include "uart_protocol.h"
}

#define UART_BAUD  115200U
#define LED_PIN    2U

static proto_parser_t g_parser;
static uint8_t        g_tx_buf[PROTO_MAX_DATA_LEN + 5U];
static uint8_t        g_tx_len;

static void handle_frame(const proto_frame_t *frame);
static void send_frame(uint8_t cmd, const uint8_t *data, uint8_t len);
static void log_error(proto_err_t err);

void setup(void)
{
	Serial.begin(UART_BAUD);
	while (!Serial) {}

	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);

	proto_parser_init(&g_parser);

	Serial.println("[PROTO] Parser ready — waiting for frames...");
}

void loop(void)
{
	uint32_t now = (uint32_t)millis();

	if (proto_check_timeout(&g_parser, now) == PROTO_ERR_TIMEOUT)
		log_error(PROTO_ERR_TIMEOUT);

	while (Serial.available() > 0)
	{
		uint8_t       byte = (uint8_t)Serial.read();
		proto_frame_t frame;

		proto_err_t err = proto_parse_byte(&g_parser, byte, now, &frame);

		switch (err)
		{
			case PROTO_OK:         handle_frame(&frame); break;
			case PROTO_INCOMPLETE: break;
			default:               log_error(err);       break;
		}
	}
}

static void handle_frame(const proto_frame_t *frame)
{
	Serial.print("[PROTO] RX CMD=0x");
	Serial.print(frame->cmd, HEX);
	Serial.print(" LEN=");
	Serial.println(frame->len);

	switch (frame->cmd)
	{
		case CMD_PING:
			send_frame(CMD_PONG, NULL, 0);
			break;

		case CMD_SET_LED:
			if (frame->len >= 1U) {
				digitalWrite(LED_PIN, frame->data[0] ? HIGH : LOW);
				uint8_t ack_data[1] = { frame->data[0] };
				send_frame(CMD_ACK, ack_data, 1U);
			} else {
				uint8_t nack_data[1] = { 0x01U };
				send_frame(CMD_NACK, nack_data, 1U);
			}
			break;

		case CMD_GET_IMU:
		{
			int16_t imu[6] = { 100, -200, 980, 10, -5, 3 };
			send_frame(CMD_IMU_DATA, (const uint8_t *)imu, sizeof(imu));
			break;
		}

		default:
		{
			uint8_t nack_data[1] = { 0x02U };
			send_frame(CMD_NACK, nack_data, 1U);
			break;
		}
	}
}

static void send_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
	if (!proto_encode(cmd, data, len, g_tx_buf, &g_tx_len)) {
		Serial.println("[PROTO] ERR: encode failed");
		return;
	}
	Serial.write(g_tx_buf, g_tx_len);
}

static void log_error(proto_err_t err)
{
	switch (err)
	{
		case PROTO_ERR_TIMEOUT:  Serial.println("[PROTO] ERR: frame timeout");       break;
		case PROTO_ERR_CHECKSUM: Serial.println("[PROTO] ERR: checksum mismatch");   break;
		case PROTO_ERR_OVERFLOW: Serial.println("[PROTO] ERR: buffer overflow");     break;
		case PROTO_ERR_BAD_END:  Serial.println("[PROTO] ERR: missing END marker");  break;
		default: break;
	}
}
