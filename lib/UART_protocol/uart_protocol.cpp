#include "uart_protocol.h"
#include <string.h>

static inline void _accumulate_checksum(proto_parser_t *p, uint8_t byte)
{
	p->checksum_calc ^= byte;
}

void proto_parser_init(proto_parser_t *p)
{
	memset(p, 0, sizeof(proto_parser_t));
	p->state = STATE_IDLE;
}

void proto_parser_reset(proto_parser_t *p)
{
	p->state         = STATE_IDLE;
	p->data_idx      = 0;
	p->checksum_calc = 0;
	p->last_byte_ms  = 0;
	memset(&p->frame, 0, sizeof(proto_frame_t));
}

proto_err_t proto_parse_byte(proto_parser_t *p,
                             uint8_t         byte,
                             uint32_t        now_ms,
                             proto_frame_t  *out_frame)
{
	p->last_byte_ms = now_ms;

	switch (p->state)
	{
		case STATE_IDLE:
			if (byte == PROTO_START)
			{
				proto_parser_reset(p);
				p->last_byte_ms = now_ms; /* Restore after reset */
				p->state = STATE_CMD;
			}
			return PROTO_INCOMPLETE;

		case STATE_CMD:
			p->frame.cmd     = byte;
			p->checksum_calc = byte;
			p->state         = STATE_LEN;
			return PROTO_INCOMPLETE;

		case STATE_LEN:
			if (byte > PROTO_MAX_DATA_LEN)
			{
				proto_parser_reset(p);
				return PROTO_ERR_OVERFLOW;
			}
			p->frame.len = byte;
			_accumulate_checksum(p, byte);

			if (byte == 0)
				p->state = STATE_CHECKSUM;
			else
			{
				p->data_idx = 0;
				p->state    = STATE_DATA;
			}
			return PROTO_INCOMPLETE;

		case STATE_DATA:
			p->frame.data[p->data_idx] = byte;
			_accumulate_checksum(p, byte);
			p->data_idx++;

			if (p->data_idx >= p->frame.len)
				p->state = STATE_CHECKSUM;
			return PROTO_INCOMPLETE;

		case STATE_CHECKSUM:
			if (byte != p->checksum_calc)
			{
				proto_parser_reset(p);
				return PROTO_ERR_CHECKSUM;
			}
			p->state = STATE_END;
			return PROTO_INCOMPLETE;

		case STATE_END:
			if (byte != PROTO_END)
			{
				proto_parser_reset(p);
				return PROTO_ERR_BAD_END;
			}
			memcpy(out_frame, &p->frame, sizeof(proto_frame_t));
			proto_parser_reset(p);
			return PROTO_OK;

		default:
			proto_parser_reset(p);
			return PROTO_INCOMPLETE;
	}
}

proto_err_t proto_check_timeout(proto_parser_t *p, uint32_t now_ms)
{
	if (p->state == STATE_IDLE)
		return PROTO_INCOMPLETE;

	/*
	 * Subtraction handles millis() wraparound correctly:
	 * uint32_t arithmetic is modular, so (now - last) works even across the
	 * ~49-day overflow boundary.
	 */
	uint32_t elapsed = now_ms - p->last_byte_ms;

	if (elapsed >= PROTO_TIMEOUT_MS)
	{
		proto_parser_reset(p);
		return PROTO_ERR_TIMEOUT;
	}

	return PROTO_INCOMPLETE;
}

bool proto_encode(uint8_t         cmd,
                  const uint8_t  *data,
                  uint8_t         len,
                  uint8_t        *out_buf,
                  uint8_t        *out_len)
{
	if (len > PROTO_MAX_DATA_LEN)
		return false;

	uint8_t checksum = cmd ^ len;
	uint8_t idx      = 0;

	out_buf[idx++] = PROTO_START;
	out_buf[idx++] = cmd;
	out_buf[idx++] = len;

	for (uint8_t i = 0; i < len; i++)
	{
		out_buf[idx++] = data[i];
		checksum      ^= data[i];
	}

	out_buf[idx++] = checksum;
	out_buf[idx++] = PROTO_END;

	*out_len = idx;
	return true;
}
