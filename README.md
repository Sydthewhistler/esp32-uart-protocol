# esp32-uart-protocol

Custom UART framing protocol implemented on ESP32 in pure C — state-machine parser with full error recovery, unit tested on host.

---

## Protocol

```
[ START | CMD | LEN | DATA | CHECKSUM | END ]
  0xAA    1B    1B   0–64B     1B       0x55
```

| Field    | Size   | Description                        |
|----------|--------|------------------------------------|
| START    | 1 byte | Frame delimiter `0xAA`             |
| CMD      | 1 byte | Command identifier                 |
| LEN      | 1 byte | Payload length (0–64 bytes)        |
| DATA     | 0–64 B | Payload                            |
| CHECKSUM | 1 byte | XOR of CMD, LEN, and DATA bytes    |
| END      | 1 byte | Frame delimiter `0x55`             |

### Command set

| Command        | Code   | Direction     | Description                        |
|----------------|--------|---------------|------------------------------------|
| `CMD_PING`     | `0x01` | Host → ESP32  | Ping — empty payload               |
| `CMD_PONG`     | `0x02` | ESP32 → Host  | Reply to PING                      |
| `CMD_SET_LED`  | `0x10` | Host → ESP32  | Set built-in LED — `DATA[0]` = 0/1 |
| `CMD_GET_IMU`  | `0x20` | Host → ESP32  | Request IMU data                   |
| `CMD_IMU_DATA` | `0x21` | ESP32 → Host  | IMU response — 6 × `int16_t` LE   |
| `CMD_ACK`      | `0xF0` | ESP32 → Host  | Generic acknowledgement            |
| `CMD_NACK`     | `0xF1` | ESP32 → Host  | Negative acknowledgement + error   |

---

## Architecture

```
esp32-uart-protocol/
├── lib/
│   └── uart_protocol/
│       ├── uart_protocol.h     # Public API, types, constants
│       └── uart_protocol.c     # Parser and encoder — pure C, no dependencies
├── src/
│   └── main.cpp                # Arduino entry point, hardware glue
├── test/
│   └── test_parser/
│       └── test_parser.c       # Unity test suite — runs on host
├── tools/
│   └── proto_tester.py         # Host-side end-to-end test script
└── platformio.ini
```

`uart_protocol.c` has no external dependencies and compiles on any target, including host.  
`main.cpp` is the only file that touches the Arduino HAL (`Serial`, `millis()`, `digitalWrite()`).

---

## Parser state machine

```
IDLE ──(0xAA)──► CMD ──► LEN ──► DATA ──► CHECKSUM ──► END ──► IDLE
  ▲                                ▲___________|          │
  │                                 (loop LEN times)      │
  └─────────────────── error / timeout ◄──────────────────┘
```

Any invalid byte or failed check resets the parser to `IDLE` immediately, allowing resynchronisation on the next valid `START` byte.

### Error handling

| Condition          | Return code           | Behaviour             |
|--------------------|-----------------------|-----------------------|
| Checksum mismatch  | `PROTO_ERR_CHECKSUM`  | Reset to IDLE         |
| LEN > 64           | `PROTO_ERR_OVERFLOW`  | Reset to IDLE         |
| Wrong END marker   | `PROTO_ERR_BAD_END`   | Reset to IDLE         |
| No byte for 100 ms | `PROTO_ERR_TIMEOUT`   | Reset to IDLE         |
| Frame in progress  | `PROTO_INCOMPLETE`    | Continue              |
| Frame complete     | `PROTO_OK`            | Populate output frame |

---

## Build & flash

```bash
pio run -e esp32dev --target upload
pio device monitor --baud 115200
```

---

## Unit tests

Parser logic is tested on host with no hardware required.

```bash
pio test -e native
```

```
test_valid_frame_no_data        [PASSED]
test_valid_frame_with_data      [PASSED]
test_bad_checksum               [PASSED]
test_overflow                   [PASSED]
test_bad_end_marker             [PASSED]
test_noise_before_start         [PASSED]
test_timeout                    [PASSED]
test_back_to_back_frames        [PASSED]
test_encode_decode_roundtrip    [PASSED]
test_encode_overflow            [PASSED]

10 Tests  0 Failures  0 Ignored
```

---

## Design notes

**Decoupled library** — the protocol implementation lives in `lib/uart_protocol` as a self-contained C module. It has no knowledge of the underlying transport and can be ported to any UART peripheral by swapping the HAL glue in `main.cpp`.

**Fault-tolerant resynchronisation** — on any error (checksum, overflow, bad marker, timeout) the parser resets to `IDLE` and scans for the next `0xAA` start byte. The protocol recovers from line noise without requiring a connection reset.

---

## References

- [UART — Wikipedia](https://en.wikipedia.org/wiki/Universal_asynchronous_receiver-transmitter) — protocol fundamentals: frame structure, baud rate, start/stop bits, parity
- [UART: A Hardware Communication Protocol — Analog Devices](https://www.analog.com/en/resources/analog-dialogue/articles/uart-a-hardware-communication-protocol.html) — framing protocol design, custom frame formats, datasheet walkthrough
- [ESP32 Technical Reference Manual — UART controller](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf) — chapter 11, register-level UART documentation
- [PlatformIO Unit Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html) — native environment setup and Unity integration
- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity) — lightweight C unit testing framework used in this project
- [CRC vs XOR checksum](https://barrgroup.com/embedded-systems/how-to/crc-calculation-c-code) — Barr Group article on checksum strategies for embedded protocols