# TDMA Radio 함수 설명

이 문서는 `tdma_radio` MVP 코드의 함수별 인자, 역할, 현재 구현 방식을 정리한다.

현재 코드는 완성 펌웨어가 아니라, Windows Desktop - STM32 Bridge - CC1101 - STM32 Gateway 구조를 잡기 위한 초기 뼈대다. STM32 HAL, GDO0 GPIO interrupt, 실제 RX FIFO 처리는 이후 보드 프로젝트에 맞춰 이어 붙여야 한다.

## 공통 프로토콜

파일:

- `common/protocol.c`
- `include/protocol.h`

### `static void put_u16(uint8_t *p, uint16_t v)`

인자:

- `p`: 2바이트를 기록할 버퍼 시작 주소.
- `v`: big-endian 형식으로 저장할 16비트 값.

역할:

- 패킷 안에 `frame_no`, `crc16` 같은 16비트 값을 넣을 때 사용한다.

구현:

- `p[0]`에 상위 바이트, `p[1]`에 하위 바이트를 저장한다.
- 내부 helper라서 `protocol.c` 밖에서는 쓰지 않는다.

### `static uint16_t get_u16(const uint8_t *p)`

인자:

- `p`: 2바이트를 읽을 버퍼 시작 주소.

역할:

- 패킷에서 big-endian 16비트 값을 읽어온다.

구현:

- `p[0]`을 상위 바이트, `p[1]`을 하위 바이트로 합쳐 `uint16_t`를 만든다.
- 내부 helper다.

### `uint16_t tdma_crc16_ccitt(const uint8_t *data, size_t len)`

인자:

- `data`: CRC를 계산할 바이트 배열.
- `len`: 계산할 바이트 수.

역할:

- TDMA payload 자체의 무결성 확인용 CRC16 값을 만든다.
- CC1101 하드웨어 CRC와 별도로, 소프트웨어 레벨의 패킷 검증에 쓴다.

구현:

- 초기값 `0xffff`, polynomial `0x1021`의 CRC-CCITT 방식으로 계산한다.
- 각 바이트를 상위 비트부터 8번 shift/xor 처리한다.

### `size_t tdma_encode_payload(const tdma_packet_t *packet, uint8_t *out, size_t out_len)`

인자:

- `packet`: 인코딩할 TDMA 패킷 구조체.
- `out`: 결과 바이트를 저장할 출력 버퍼.
- `out_len`: 출력 버퍼 크기.

역할:

- `tdma_packet_t` 구조체를 무선으로 보낼 수 있는 바이트 배열로 바꾼다.

구현:

- 고정 헤더 8바이트를 만든다.
  - `network_id`
  - `type`
  - `src`
  - `dst`
  - `frame_no`
  - `slot_no`
  - `payload_len`
- 뒤에 payload를 복사한다.
- 마지막 2바이트에 `tdma_crc16_ccitt()` 결과를 붙인다.
- 성공하면 전체 길이를 반환하고, 인자 오류나 버퍼 부족이면 `0`을 반환한다.

### `int tdma_decode_payload(const uint8_t *data, size_t len, tdma_packet_t *packet)`

인자:

- `data`: 수신한 TDMA payload 바이트 배열.
- `len`: 수신한 바이트 수.
- `packet`: 디코딩 결과를 저장할 구조체.

역할:

- 무선에서 받은 TDMA payload를 `tdma_packet_t` 구조체로 복원한다.

구현:

- 최소 길이, payload 길이, 최대 payload 크기를 검사한다.
- 마지막 2바이트의 CRC와 실제 계산한 CRC를 비교한다.
- 정상일 때만 필드를 구조체에 복사한다.
- 반환값:
  - `0`: 정상
  - `-1`: null 또는 너무 짧음
  - `-2`: payload 길이 불일치
  - `-3`: CRC 오류

### `size_t cc1101_wrap_variable_packet(uint8_t cc1101_addr, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_len)`

인자:

- `cc1101_addr`: CC1101 address check용 수신 주소.
- `payload`: TDMA payload.
- `payload_len`: TDMA payload 길이.
- `out`: CC1101 TX FIFO에 넣을 최종 바이트 배열.
- `out_len`: 출력 버퍼 크기.

역할:

- RF Studio 설정의 variable packet length 형식에 맞춰 `[length][address][payload...]` 구조를 만든다.

구현:

- `out[0]`에는 address 1바이트와 payload 길이를 더한 값을 넣는다.
- `out[1]`에는 CC1101 주소를 넣는다.
- `out[2...]`에는 TDMA payload를 복사한다.
- payload가 254바이트를 넘거나 버퍼가 부족하면 `0`을 반환한다.

## TDMA 시간 동기화

파일:

- `common/tdma.c`
- `include/tdma.h`

### `void tdma_clock_init(tdma_clock_t *clock)`

인자:

- `clock`: 초기화할 TDMA clock 구조체.

역할:

- 라즈베리나 노드가 사용할 TDMA 로컬 시계 상태를 초기화한다.

구현:

- `config.h`의 `TDMA_FRAME_PERIOD_US`, `TDMA_SLOT_US`, `TDMA_GUARD_US`를 넣는다.
- `frame_no`, `frame_start_local_us`, `sync_offset_us`는 0으로 둔다.
- 아직 비콘을 못 받은 상태이므로 `locked=0`으로 둔다.

### `void tdma_clock_sync_beacon(tdma_clock_t *clock, uint16_t frame_no, int64_t beacon_rx_us)`

인자:

- `clock`: 보정할 TDMA clock.
- `frame_no`: 비콘 안에 들어 있던 프레임 번호.
- `beacon_rx_us`: 로컬 monotonic clock 기준 비콘 수신 시각.

역할:

- STM32 마스터 비콘을 기준으로 라즈베리 로컬 TDMA 시계를 맞춘다.

구현:

- 첫 비콘이면 `beacon_rx_us`를 프레임 시작으로 보고 lock한다.
- 이미 lock된 상태면 예상 비콘 시각과 실제 수신 시각의 오차를 계산한다.
- 오차를 한 번에 반영하지 않고 `error / 8`만 `sync_offset_us`에 더해 부드럽게 보정한다.
- 간단한 PLL처럼 동작하도록 만든 초기 구현이다.

### `int64_t tdma_slot_start_us(const tdma_clock_t *clock, uint8_t slot_no)`

인자:

- `clock`: 기준 TDMA clock.
- `slot_no`: 시작 시각을 알고 싶은 슬롯 번호.

역할:

- 현재 프레임에서 특정 슬롯의 로컬 시작 시각을 계산한다.

구현:

- `frame_start_local_us + slot_no * slot_us`를 반환한다.

### `uint8_t tdma_slot_at(const tdma_clock_t *clock, int64_t now_us)`

인자:

- `clock`: 기준 TDMA clock.
- `now_us`: 로컬 monotonic clock 기준 현재 시각.

역할:

- 현재 시간이 몇 번 슬롯에 해당하는지 계산한다.

구현:

- lock되지 않았거나 현재 시간이 프레임 시작 전이면 `0xff`를 반환한다.
- 프레임 시작부터 지난 시간을 frame period로 나눈 나머지를 구한다.
- 그 값을 slot 길이로 나눠 슬롯 번호를 얻는다.

### `int tdma_is_inside_guard(const tdma_clock_t *clock, int64_t now_us)`

인자:

- `clock`: 기준 TDMA clock.
- `now_us`: 로컬 현재 시각.

역할:

- 현재 시간이 guard time 안인지 확인한다.

구현:

- lock되지 않았거나 프레임 시작 전이면 안전하게 guard 안이라고 본다.
- 현재 슬롯 내부 위치가 슬롯 끝의 `slot_us - guard_us` 이후이면 `1`을 반환한다.
- 아니면 `0`을 반환한다.

## 노드 테이블

파일:

- `common/node_table.c`
- `include/node_table.h`

### `void node_table_init(tdma_node_table_t *table)`

인자:

- `table`: 초기화할 노드 테이블.

역할:

- 마스터가 관리하는 노드 목록을 빈 상태로 만든다.

구현:

- 최대 `TDMA_MAX_NODES` 개수만큼 순회한다.
- `active=0`, `slot_no=0xff`, RSSI/LQI/프레임 정보를 0으로 초기화한다.

### `tdma_node_t *node_table_find(tdma_node_table_t *table, uint8_t node_id)`

인자:

- `table`: 검색할 노드 테이블.
- `node_id`: 찾을 노드 ID.

역할:

- 등록된 노드 중 특정 ID를 찾는다.

구현:

- `active=1`이고 `node_id`가 같은 항목을 반환한다.
- 없으면 `0`을 반환한다.

### `tdma_node_t *node_table_upsert(tdma_node_table_t *table, uint8_t node_id)`

인자:

- `table`: 수정할 노드 테이블.
- `node_id`: 찾거나 새로 만들 노드 ID.

역할:

- 이미 있으면 기존 노드를 반환하고, 없으면 빈 칸에 새 노드를 만든다.

구현:

- 먼저 `node_table_find()`로 기존 항목을 찾는다.
- 없으면 `active=0`인 첫 칸을 찾아 `active=1`, `node_id=node_id`, `slot_no=0xff`로 만든다.
- 테이블이 꽉 차면 `0`을 반환한다.

### `void node_table_assign_fixed_slots(tdma_node_table_t *table)`

인자:

- `table`: 슬롯을 배정할 노드 테이블.

역할:

- 활성 노드에 고정 슬롯 번호를 순서대로 부여한다.

구현:

- slot 0은 비콘용으로 비우고, slot 1도 여유/제어용으로 남긴다는 가정으로 slot 2부터 배정한다.
- 활성 노드마다 `slot_no`를 하나씩 증가시키며 넣는다.

## Windows Desktop - STM32 브리지

파일:

- `desktop_win/serial_win.c`
- `desktop_win/stm32_bridge_link.c`
- `desktop_win/main_desktop.c`
- `include/serial_win.h`
- `include/stm32_bridge_link.h`

### `int serial_win_open(serial_win_t *serial, const char *port_name, uint32_t baud)`

인자:

- `serial`: 열린 COM 포트 핸들을 저장할 구조체.
- `port_name`: 예: `COM3`.
- `baud`: 시리얼 속도. 현재 기본값은 `WIN_BRIDGE_BAUD=115200`.

역할:

- Windows에서 STM32 USB CDC 가상 COM 포트를 연다.

구현:

- `_WIN32`일 때 `\\\\.\\COMx` 형식으로 `CreateFileA()`를 호출한다.
- `GetCommState()`, `SetCommState()`로 8N1, 지정 baud를 설정한다.
- `SetCommTimeouts()`로 짧은 read/write timeout을 설정한다.
- Windows가 아니면 `-1`을 반환한다.

### `int serial_win_write(serial_win_t *serial, const uint8_t *data, size_t len)`

인자:

- `serial`: 열린 COM 포트.
- `data`: 보낼 바이트 배열.
- `len`: 보낼 길이.

역할:

- STM32 브리지로 raw 바이트를 보낸다.

구현:

- Windows에서는 `WriteFile()`을 호출한다.
- 성공하면 실제 쓴 바이트 수를 반환한다.
- 실패하면 `-1`을 반환한다.

### `int serial_win_read(serial_win_t *serial, uint8_t *data, size_t max_len)`

인자:

- `serial`: 열린 COM 포트.
- `data`: 읽은 바이트를 저장할 버퍼.
- `max_len`: 최대 읽기 길이.

역할:

- STM32 브리지에서 raw 바이트를 읽는다.

구현:

- Windows에서는 `ReadFile()`을 호출한다.
- timeout 안에 읽힌 바이트 수를 반환한다.
- 실패하면 `-1`을 반환한다.

### `void serial_win_close(serial_win_t *serial)`

인자:

- `serial`: 닫을 COM 포트 구조체.

역할:

- Windows COM 포트를 닫는다.

구현:

- 유효한 Windows handle이면 `CloseHandle()`을 호출한다.
- 이후 handle을 invalid 상태로 바꾼다.

### `static size_t bridge_build_frame(uint8_t type, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_len)`

인자:

- `type`: STM32 bridge command/event type.
- `payload`: 명령 payload.
- `payload_len`: payload 길이.
- `out`: bridge frame 출력 버퍼.
- `out_len`: 출력 버퍼 크기.

역할:

- Windows와 STM32 사이에서 쓰는 USB CDC 프레임을 만든다.

구현:

- 프레임 구조는 `[0xA5][type][payload_len][payload...][crc16]`이다.
- CRC는 앞의 magic/type/length/payload에 대해 `tdma_crc16_ccitt()`로 계산한다.
- 내부 helper다.

### `int bridge_open(stm32_bridge_link_t *link, const char *port_name)`

인자:

- `link`: STM32 bridge 연결 상태 구조체.
- `port_name`: 예: `COM3`.

역할:

- Desktop master가 STM32 bridge와 통신할 수 있게 COM 포트를 연다.

구현:

- 내부에서 `serial_win_open()`을 호출한다.
- baud는 `config.h`의 `WIN_BRIDGE_BAUD`를 사용한다.

### `int bridge_start_rx(stm32_bridge_link_t *link)`

인자:

- `link`: 열린 STM32 bridge 연결.

역할:

- STM32에게 CC1101을 RX 상태로 두라고 요청한다.

구현:

- `BRIDGE_CMD_START_RX` 명령을 bridge frame으로 만든다.
- `serial_win_write()`로 STM32에 전송한다.
- 전송 길이가 맞으면 `0`, 아니면 `-1`을 반환한다.

### `int bridge_send_packet(stm32_bridge_link_t *link, const uint8_t *data, size_t len)`

인자:

- `link`: 열린 STM32 bridge 연결.
- `data`: CC1101 TX FIFO로 보낼 최종 바이트 배열.
- `len`: 보낼 바이트 수.

역할:

- Desktop master가 만든 무선 패킷을 STM32에게 송신 지시한다.

구현:

- `BRIDGE_CMD_TX_PACKET` 프레임으로 감싼다.
- STM32로 전송한다.
- 성공하면 원래 payload 길이를 반환한다.
- frame 생성 실패는 `-1`, serial write 실패는 `-2`를 반환한다.

### `int bridge_poll_packet(stm32_bridge_link_t *link, uint8_t *data, size_t max_len)`

인자:

- `link`: 열린 STM32 bridge 연결.
- `data`: 수신 payload 저장 버퍼.
- `max_len`: 저장 가능한 최대 길이.

역할:

- STM32가 올려준 수신 패킷 이벤트를 읽는다.

구현:

- bridge header 3바이트를 먼저 읽는다.
- magic이 `0xA5`이고 type이 `BRIDGE_EVT_RX_PACKET`인지 확인한다.
- payload와 CRC를 읽고 CRC를 검증한다.
- 정상일 때 payload 길이를 반환한다.
- 읽을 것이 없으면 `0`, 오류는 음수로 반환한다.

### `void bridge_close(stm32_bridge_link_t *link)`

인자:

- `link`: 닫을 STM32 bridge 연결.

역할:

- STM32 USB CDC 연결을 닫는다.

구현:

- 내부에서 `serial_win_close()`를 호출한다.

### `static size_t build_beacon(uint16_t frame_no, uint8_t *out, size_t out_len)`

인자:

- `frame_no`: 생성할 TDMA 프레임 번호.
- `out`: TDMA payload 출력 버퍼.
- `out_len`: 출력 버퍼 크기.

역할:

- Desktop master가 보낼 TDMA beacon payload를 만든다.

구현:

- `tdma_packet_t`를 `TDMA_PKT_BEACON` 타입으로 채운다.
- payload에는 frame period, slot length, guard time, slot table version을 넣는다.
- `tdma_encode_payload()`로 TDMA payload 바이트 배열을 만든다.

### `int main(int argc, char **argv)` in `desktop_win/main_desktop.c`

인자:

- `argc`, `argv`: 실행 인자. 첫 번째 인자로 COM 포트를 받을 수 있다.

역할:

- Windows Desktop master의 초기 실행 진입점이다.

구현:

- 인자가 있으면 해당 COM 포트를 쓰고, 없으면 `COM3`를 쓴다.
- STM32 bridge를 열고 RX 시작 명령을 보낸다.
- frame 0 beacon을 만들고 CC1101 variable packet 형식으로 감싼다.
- STM32에 TX packet 명령을 보낸 뒤 종료한다.
- 현재는 지속 루프가 아니라 “비콘 1회 송신” MVP다.

## STM32 Bridge

파일:

- `stm32_bridge/cc1101.c`
- `stm32_bridge/usb_cdc_bridge.c`
- `stm32_bridge/main_stm32.c`

### `static int cc1101_write_reg(cc1101_t *radio, uint8_t addr, uint8_t value)`

인자:

- `radio`: STM32 쪽 CC1101 핸들.
- `addr`: 쓸 CC1101 레지스터 주소.
- `value`: 쓸 값.

역할:

- CC1101 단일 레지스터 write용 내부 함수다.

구현:

- 현재는 STM32 HAL SPI가 연결되지 않은 placeholder다.
- 인자를 사용 처리만 하고 `0`을 반환한다.
- 이후 `HAL_GPIO_WritePin(CSN low)`, `HAL_SPI_Transmit()`, `CSN high` 흐름으로 채워야 한다.

### `static int cc1101_write_burst(cc1101_t *radio, uint8_t addr, const uint8_t *data, size_t len)`

인자:

- `radio`: STM32 쪽 CC1101 핸들.
- `addr`: burst write 시작 주소.
- `data`: 쓸 바이트 배열.
- `len`: 쓸 길이.

역할:

- CC1101 PATABLE 또는 FIFO처럼 여러 바이트를 연속으로 쓸 때 사용한다.

구현:

- 현재는 placeholder다.
- 이후 `addr | 0x40` burst bit와 함께 SPI transmit으로 구현해야 한다.

### `int cc1101_apply_rf_preset(cc1101_t *radio)`

인자:

- `radio`: RF preset을 적용할 STM32 쪽 CC1101 핸들.

역할:

- 기존 RF Studio 로그에서 가져온 CC1101 레지스터 값을 STM32 쪽 CC1101에 적용한다.

구현:

- `common/cc1101_regs.c`의 `cc1101_rf_preset[]` 배열을 순회한다.
- 각 항목을 `cc1101_write_reg()`로 쓴다.
- 마지막에 `cc1101_pa_table` 8바이트를 `CC1101_PATABLE`에 burst write한다.
- 지금은 하위 write 함수가 placeholder라 실제 SPI 송신은 아직 되지 않는다.

### `int usb_cdc_bridge_parse(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)`

인자:

- `in`: PC에서 받은 USB CDC 입력 버퍼.
- `in_len`: 입력 길이.
- `out`: PC로 돌려보낼 출력 버퍼.
- `out_len`: 출력 버퍼 크기.

역할:

- STM32에서 PC bridge frame을 해석할 자리다.

구현:

- 현재는 placeholder로 `0`을 반환한다.
- 이후 `[0xA5][type][len][payload][crc16]`를 파싱하고, `BRIDGE_CMD_TX_PACKET`, `BRIDGE_CMD_START_RX` 등을 처리해야 한다.

### `int main(void)` in `stm32_bridge/main_stm32.c`

인자:

- 없음.

역할:

- STM32 firmware 진입점 자리다.

구현:

- 현재는 무한 루프만 있다.
- 이후 CubeMX/HAL 초기화, USB CDC command loop, TIM 기반 beacon scheduling, CC1101 GDO0 EXTI 처리를 붙여야 한다.

## 추상 Radio Link 인터페이스

파일:

- `include/radio_link.h`

이 헤더는 아직 구현 파일이 없다. 나중에 Windows bridge, STM32 direct radio 등을 같은 상위 로직에서 다루기 위한 추상 인터페이스다.

### `int radio_link_open(radio_link_t *radio)`

역할:

- 특정 radio backend를 연다.

현재 상태:

- 선언만 있고 구현은 없다.

### `int radio_link_configure(radio_link_t *radio)`

역할:

- CC1101 RF preset 또는 backend 설정을 적용한다.

현재 상태:

- 선언만 있고 구현은 없다.

### `int radio_link_start_rx(radio_link_t *radio)`

역할:

- radio를 RX 상태로 전환한다.

현재 상태:

- 선언만 있고 구현은 없다.

### `int radio_link_send(radio_link_t *radio, const uint8_t *data, size_t len)`

인자:

- `radio`: 보낼 radio backend.
- `data`: 보낼 바이트 배열.
- `len`: 보낼 길이.

역할:

- 추상 radio backend를 통해 패킷을 송신한다.

현재 상태:

- 선언만 있고 구현은 없다.

### `int radio_link_poll_rx(radio_link_t *radio, uint8_t *data, size_t max_len, radio_rx_meta_t *meta)`

인자:

- `radio`: 수신을 확인할 radio backend.
- `data`: 수신 데이터를 저장할 버퍼.
- `max_len`: 버퍼 크기.
- `meta`: RSSI, LQI, CRC OK 같은 수신 부가정보.

역할:

- 수신된 패킷이 있으면 읽어온다.

현재 상태:

- 선언만 있고 구현은 없다.

### `void radio_link_close(radio_link_t *radio)`

역할:

- radio backend를 닫는다.

현재 상태:

- 선언만 있고 구현은 없다.

