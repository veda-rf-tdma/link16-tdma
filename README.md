# TDMA 무선 MVP

이 폴더는 STM32F401RE + CC1101 TDMA 실험을 위한 초기 C 코드 골격을 저장합니다.

## 역할

- Windows 데스크톱: TDMA 컨트롤러 및 운영자 콘솔.
- STM32 브리지: USB CDC 브리지, CC1101 SPI 마스터 및 정밀 TDMA 타이밍 마스터.
- STM32 게이트웨이: 동기화된 수신기/게이트웨이 로거.

## 파일 맵

### 프로젝트 루트

- `CMakeLists.txt`: 공유 TDMA 라이브러리와 데스크톱 테스트 실행 파일을 빌드합니다.
- `README.md`: 고수준 빌드, 실행, RF 프리셋, 패킷 구조 및 타이밍 메모를 제공합니다.
- `FUNCTIONS.md`: 현재 MVP 코드에 대한 함수별 한글 설명을 제공합니다.
- `rssi_log.csv`: 로컬 RSSI 로거 출력 샘플/데이터 파일입니다. 소스 코드가 아닌 생성된 실험 데이터로 취급합니다.

### 공유 코드

- `common/protocol.c`: TDMA 페이로드를 인코딩 및 디코딩하고, 소프트웨어 CRC16을 계산하며, TDMA 페이로드를 CC1101 가변 길이 패킷으로 래핑합니다.
- `common/tdma.c`: 로컬 TDMA 클록, 비콘 동기화, 슬롯 조회 및 가드 타임 점검을 유지합니다.
- `common/tdma_runtime.c`: 슬롯 시작, 채널 전환, TX/RX 동작, 안정화 시간, 가드 타임 및 GDO0 완료 이벤트에 대한 플랫폼 독립적인 TDMA 런타임 상태 머신입니다.
- `common/cc1101_regs.c`: 공유되는 SmartRF Studio CC1101 레지스터 프리셋과 PA 테이블을 저장합니다.
- `common/radio_metrics.c`: CC1101 RSSI 바이트를 dBm으로 변환하고 RSSI를 기반으로 대략적인 거리를 추정합니다.
- `common/node_table.c`: MVP 노드 테이블 API에서 사용하는 TDMA 노드 기록을 추적합니다.

### 공용 헤더

- `include/config.h`: TDMA 타이밍, 노드 주소, 브리지 보레이트(Baud rate) 및 RF 매개변수와 같은 프로젝트 전반의 상수를 정의합니다.
- `include/protocol.h`: TDMA 패킷 유형, 패킷 구조, 비콘 페이로드 구조 및 프로토콜 인코딩/디코딩 선언을 포함합니다.
- `include/tdma.h`: TDMA 클록 상태 및 타이밍/동기화 함수 선언을 포함합니다.
- `include/tdma_runtime.h`: STM32 타이머 및 GDO0 통합에 사용되는 TDMA 런타임 상태/동작 선언을 포함합니다.
- `include/cc1101_regs.h`: CC1101 레지스터 주소 및 공유 RF 프리셋 선언을 포함합니다.
- `include/radio_metrics.h`: RSSI 변환 및 거리 추정 선언을 포함합니다.
- `include/node_table.h`: 노드 테이블 데이터 구조 및 함수 선언을 포함합니다.
- `include/radio_link.h`: 향후 플랫폼 독립적인 무선 처리를 위한 일반 무선 링크 인터페이스 형식을 정의합니다.
- `include/serial_win.h`: Windows 직렬 포트 래퍼 인터페이스입니다.
- `include/stm32_bridge_link.h`: 데스크톱 측 USB CDC 브리지 명령/이벤트 인터페이스입니다.
- `include/cc1101_stm32.h`: Cube/HAL 통합을 위한 STM32 CC1101 드라이버 인터페이스입니다.
- `include/usb_cdc_bridge.h`: STM32 측 USB CDC 브리지 인터페이스입니다.
- `include/stm32_raw_tx_demo.h`: STM32 원시 송신(Raw TX) 데모 진입점 선언입니다.

### 데스크톱 코드

- `desktop_win/main_desktop.c`: Windows 데스크톱 마스터 실행 파일입니다. STM32 브리지를 통해 주기적으로 TDMA 비콘을 송신하고 수신된 패킷을 로깅합니다.
- `desktop_win/stm32_bridge_link.c`: 데스크톱과 STM32 간의 USB CDC 브리지 프레임을 인코딩/디코딩합니다.
- `desktop_win/serial_win.c`: Windows COM 포트 열기/읽기/쓰기/닫기 구현입니다.

### STM32 브리지 코드

- `stm32_bridge/STM32_SETUP.md`: STM32 브리지를 위한 CubeIDE 설정 메모 및 코드 통합 지점 문서입니다.
- `stm32_bridge/main_stm32.c`: 의도된 타이밍 소유권을 문서화하는 STM32 펌웨어 메인 루프 플레이스홀더입니다.
- `stm32_bridge/cc1101.c`: STM32/HAL 측 CC1101 SPI 제어 구현입니다.
- `stm32_bridge/usb_cdc_bridge.c`: STM32 측 USB CDC 브리지 명령 파서 및 이벤트 전송 코드입니다.
- `stm32_bridge/stm32_raw_tx_demo.c`: STM32 원시 CC1101 송신 데모 코드입니다.

### 분석 코드

- `analysis/calibration_config.json`: 실험을 통해 튜닝 가능한 RSSI, 아폴로니우스(Apollonius), EKF 관측 설정입니다. 실제 데이터를 수집한 후 이 파일을 수정합니다.
- `analysis/analyze_rssi.py`: RSSI 로그를 정규화된 전력비, 패킷 드롭 이벤트, 아폴로니우스 후보 및 EKF 지원 관측 데이터로 변환합니다.
- `analysis/sample_log.csv`: 하드웨어 데이터가 준비되기 전에 분석 파이프라인을 확인하기 위한 소규모 3-앵커 샘플 로그입니다.

## 빌드 및 실행

### RSSI 사후 처리 및 캘리브레이션 옵션

RSSI 로그를 수집한 후, 다음 파일에서 분석 설정을 조정합니다:

```text
analysis/calibration_config.json
```

이 파일의 값은 초기 분석 매개변수이며, 최종 측정 상수가 아닙니다. 기준 RSSI/PDR 측정 후에 업데이트하십시오:

```json
{
  "rssi_noise_floor_dbm": -104.5,
  "rssi_drop_threshold_db": -2.4,
  "rssi_moving_avg_window": 5,
  "ekf_process_noise": 0.08,
  "ekf_measurement_noise": 1.2
}
```

샘플 분석 실행:

```bash
python analysis/analyze_rssi.py
```

측정된 로거 CSV에 대한 분석 실행:

```bash
python analysis/analyze_rssi.py --input rssi_log.csv --config analysis/calibration_config.json --out-dir analysis_out
```

생성되는 파일들:

```text
analysis_out/normalized_rssi.csv
analysis_out/drop_events.csv
analysis_out/apollonius_candidates.csv
analysis_out/ekf_observations.csv
analysis_out/summary.json
```

EKF 궤적 추적 실행:

```bash
# 트래커 빌드
cmake --build build --target tdma_ekf_tracker

# 트래커 실행 (인수: [입력_관측데이터_csv] [출력_궤적_csv])
./build/tdma_ekf_tracker analysis_out/ekf_observations.csv analysis_out/ekf_trajectory.csv
```

생성된 궤적:

```text
analysis_out/ekf_trajectory.csv
```

3-앵커 정규화의 경우, 측정된 CSV에 `receiver_id`가 포함되는 것이 좋습니다. `receiver_id`가 없는 기존 로그도 허용되지만, 모든 행이 `calibration_config.json`의 `default_receiver_id`에서 온 것처럼 처리됩니다.

### 데스크톱 마스터

Windows 데스크톱 마스터는 STM32 USB CDC 브리지를 통해 주기적으로 TDMA 비콘을 송신하고 CSV 로그를 기록합니다.
STM32가 RX 이벤트를 보낼 때, 데스크톱 로그에는 CC1101 RSSI/LQI와 대략적인 거리 추정치가 포함됩니다.

```powershell
cd link16-tdma
cmake -S . -B build
cmake --build build --target tdma_desktop
.\build\Debug\tdma_desktop.exe COM3 100 desktop_master_log.csv
```

인수(Arguments):

```text
tdma_desktop.exe <COM 포트> <프레임 수> <로그 CSV 경로>
```

예시:

```text
tdma_desktop.exe COM3 100 desktop_master_log.csv
```

RX 로그 예시:

```text
RX_OK frame=2 addr=0x00 type=4 src=0x10 dst=0xff packet_frame=2 slot=2 payload_len=12 rssi_raw=0x9a rssi=-49.0 dBm lqi=0x80 est_distance=1.42 m
```

데스크톱 CSV 컬럼 구조:

```text
timestamp,event,frame,receiver_id,receiver_role,tx_node_id,tx_role,slot_no,slot_role,channel,rssi_raw,rssi_dbm,lqi,detail
```

### STM32 브리지

STM32CubeIDE 설정 및 코드 삽입 지점은 다음 문서에 설명되어 있습니다:

```text
stm32_bridge/STM32_SETUP.md
```

## RF 프리셋

기본 CC1101 레지스터 테이블은 `common/cc1101_regs.c`에 저장됩니다.

이 설정은 다음 프로젝트 로그를 기반으로 합니다:

- `Projects/Link16-TDMA/Logs/2026-05-13 RF Studio 기반 CC1101 세팅 정리.md`

현재 프리셋 값:

```text
- 반송파 주파수: 433.919830 MHz (현재 433.92 MHz COTS PoC 설정으로 사용됨)
- 변조 방식: 2-FSK
- 전송 속도: 37.9868 kBaud
- 패킷 모드: 가변 길이 (Variable length)
- 주소 확인: 활성화 (Enabled)
- CRC: 활성화 (Enabled)
- PA 테이블 첫 번째 항목: 5 dBm에 대해 0x85 설정
```

하드웨어 정렬 TDMA 상수:

```text
- 슬롯 주기: 10.00 ms
- 페이로드 TX/RX 윈도우: 8.00 ms
- PLL 및 상태 안정화 마진: 1.20 ms
- 가드 타임: 0.80 ms
- STM32 SPI 목표 속도: 6.5 MHz, SPI 모드 0, 소프트웨어 CSN 제어
- 논리적 노드 세트: 마스터 앵커 0x21, 앵커 1 0x22, 앵커 2 0x23, 항공기/이동체 노드 0x31
- 슬롯 맵: 슬롯 0 마스터 비콘, 슬롯 1 항공기 송신, 슬롯 2 앵커 1 보고, 슬롯 3 앵커 2 보고
- FHSS(주파수 호핑) 정책 스터브: tdma_channel_for_slot(frame, slot)은 각 프레임/슬롯을 설정된 CC1101 채널 인덱스로 매핑합니다.
- ATPC(송신 전력 제어) 정책 스터브: tdma_tx_power_for_margin(margin_db)은 설정된 최소/최대 제한 범위 내에서 기본 송신 전력을 조정합니다.
- 런타임 상태 머신: tdma_runtime_tick()은 SET_CHANNEL, START_TX, START_RX, STOP_RADIO, LOG_RX 동작을 생성합니다. tdma_runtime_on_gdo0_edge()는 STM32 EXTI 콜백의 GDO0 완료 후크입니다.
```

## CC1101 패킷 구조

CC1101 FIFO 데이터는 SmartRF Studio 가변 길이 형식을 사용합니다:

```text
[length][address][TDMA payload...]
```

TDMA 페이로드는 `common/protocol.c`에 의해 인코딩되며 다음을 포함합니다:

```text
network_id, type, src, dst, frame_no, slot_no, payload_len, payload, crc16
```

## 타이밍 모델

STM32 브리지는 하드웨어 타이머를 기반으로 비콘을 송신하며, 수신 노드는 비콘 수신 시점을 기준으로 로컬 모노토닉 클록(Monotonic clock)을 동기화합니다.

데스크톱 PC가 정책 및 슬롯 맵을 수정할 수 있으나, 마이크로초 단위의 서브 프레임(sub-frame) 수준의 정밀한 타이밍 제어는 데스크톱에 의존하지 않고 STM32 내부 하드웨어 타이머로 처리됩니다.
