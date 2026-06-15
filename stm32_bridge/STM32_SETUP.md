# STM32 Bridge Setup

이 문서는 `Desktop master -> STM32 USB CDC bridge -> CC1101 -> STM32 gateway` 테스트를 위해 STM32CubeIDE 프로젝트에 어떤 설정과 코드를 넣어야 하는지 정리한다.

## Target Role

STM32는 PC와 CC1101 사이의 브리지다.

- PC에서 USB CDC로 bridge frame 수신
- CC1101 RF preset 적용
- PC 명령에 따라 CC1101 RX 시작
- PC 명령에 따라 CC1101 TX FIFO 송신
- CC1101 RX FIFO에 패킷이 있으면 PC로 RX event 전송

## Hardware Pins

기존 문서 기준 STM32F401RE + CC1101 연결:

| CC1101 | STM32F401RE | CubeMX 기능 |
|---|---|---|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| SCK | PA5 / D13 | SPI1_SCK |
| MISO | PA6 / D12 | SPI1_MISO |
| MOSI | PA7 / D11 | SPI1_MOSI |
| CSN | PA4 / D10 | GPIO_Output |
| GDO0 | PB0 또는 D2 | GPIO_EXTI, 실제 배선 기준으로 하나 선택 |
| GDO2 | 미사용 | Not connected |

주의:

- CC1101 VCC는 반드시 3.3V.
- `PA5`는 SPI1_SCK로 써야 하며 LED GPIO로 쓰면 안 된다.
- `CSN`은 SPI hardware NSS가 아니라 GPIO output으로 직접 제어한다.

## CubeMX Settings (Pinout & Configuration 상세 설정)

CubeMX의 **Pinout & Configuration** 탭 및 **Clock Configuration**, **Project Manager**에서 아래와 같이 설정합니다.

### 1. Pinout & Configuration 설정

#### **System Core -> SYS**
- **Debug**: `Serial Wire` (ST-Link 디버깅 및 펌웨어 업로드를 위해 필수)
- **Timebase Source**: `SysTick` (기본값)

#### **System Core -> NVIC**
- **EXTI line0 interrupt** (`PB0` EXTI 활성화 시): **Enabled** 체크 (CC1101의 수신 완료 인터럽트 처리에 필요)
- **USB On The Go FS global interrupt** (USB 사용 시): **Enabled** 체크

#### **System Core -> GPIO**
- **PA4**: `GPIO_Output`으로 설정
  - **GPIO output level**: `High` (CC1101 CSN은 Idle 상태에서 High여야 함)
  - **GPIO mode**: `Output Push Pull`
  - **GPIO Pull-up/Pull-down**: `No pull-up and no pull-down`
  - **Maximum output speed**: `Medium` 또는 `High`
  - **User Label**: `CC1101_CSN`
- **PB0** (또는 GDO0 연결 핀): `GPIO_EXTI0`으로 설정
  - **GPIO mode**: `External Interrupt Mode with Rising/Falling edge trigger detection` (CC1101 GDO0의 신호 변화 감지)
  - **GPIO Pull-up/Pull-down**: `No pull-up and no pull-down` 또는 `Pull-down` (회로에 따라 설정)
  - **User Label**: `CC1101_GDO0`
- **PA8** (상태 표시용 LED, 필요한 경우): `GPIO_Output`
  - PA5는 SPI1_SCK와 겹쳐 온보드 LED(LD2)를 쓸 수 없으므로, 외부 LED를 제어하기 위해 설정

#### **Connectivity -> SPI1**
- **Mode**: `Full-Duplex Master`
- **Hardware NSS Signal**: `Disable` (PA4 GPIO로 직접 제어함)
- **Basic Parameters**:
  - **Frame Format**: `Motorola`
  - **Data Size**: `8 Bits`
  - **First Bit**: `MSB First`
- **Clock Parameters**:
  - **Prescaler (Divisor)**: 초기 디버깅 시에는 `1 MHz` 근처 속도(예: 64 분주 등)로 설정하여 안정성을 확인한 후, 최종 속도인 **6.5 MHz** 근처로 설정(예: 84MHz 클럭 기준 16 분주 시 5.25MHz, 8 분주 시 10.5MHz이므로 보드 클럭과 분주비를 맞추어 6.5MHz 이하로 설정).
  - **Clock Polarity (CPOL)**: `Low` (SPI Mode 0)
  - **Clock Phase (CPHA)**: `1 Edge` (SPI Mode 0)
  - **Baud Rate**: 보드 최대 속도에 따라 분주비 자동 계산

#### **Connectivity -> USB_OTG_FS** (데스크톱 브리지 노드만 해당)
- **Mode**: `Device_Only`
- **Activate_VBUS**: `Disable` (PC 연결 시 감지선 미사용 시) 또는 회로 구성에 맞춤

#### **Middleware -> USB_DEVICE** (데스크톱 브리지 노드만 해당)
- **Class For FS IP**: `Communication Device Class (Virtual Port Com)` (CDC로 설정하여 가상 직렬 포트 생성)

---

### 2. Clock Configuration (클럭 트리 설정)

USB CDC 가상 포트를 안정적으로 사용하려면 **USB 클럭이 반드시 48 MHz**여야 합니다.

1. **Input Frequency**: Nucleo 보드의 8MHz 외부 크리스탈(HSE) 또는 내부 HSI 선택
2. **PLL Source Mux**: `HSE` 또는 `HSI`
3. **System Clock Mux**: `PLLCLK` 선택하여 코어 클럭 최대치(F401RE 기준 84 MHz)로 설정
4. **USB Clock Mux (To USB OTG FS)**: PLL의 48MHz 출력이 인가되도록 PLL 인자(`PLLM`, `PLLN`, `PLLQ`)를 조정
   - 예: HSI (16MHz) -> /16 -> *336 -> /7 (PLLQ) = 48MHz USB clock 설정

---

### 3. Project Manager 설정 (코드 생성 옵션)

- **Project** 탭:
  - **Toolchain / IDE**: `STM32CubeIDE`
- **Code Generator** 탭:
  - **Generated files**: `Generate peripheral initialization as a pair of '.c/.h' files per peripheral` 체크 (각 장치별 드라이버 파일을 독립적으로 분리하여 소스 관리 가독성 향상)
  - **Keep User Code when re-generating**: 항상 체크 상태로 유지하여 코드 재생성 시 기존 코드가 지워지지 않도록 보호

## Files To Copy Into CubeMX Project

CubeMX 프로젝트의 `Core/Inc` 또는 별도 include path에 복사:

```text
include/config.h
include/protocol.h
include/cc1101_regs.h
include/cc1101_stm32.h
include/usb_cdc_bridge.h
include/stm32_bridge_link.h
```

CubeMX 프로젝트의 `Core/Src`에 복사:

```text
common/protocol.c
common/cc1101_regs.c
stm32_bridge/cc1101.c
stm32_bridge/usb_cdc_bridge.c
```

`tdma.c`, `node_table.c`는 STM32 bridge 1차 테스트에는 없어도 된다.

RSSI logger 1차 테스트처럼 STM32가 단순히 주기 송신만 하면 되는 경우에는 아래 파일도 복사한다.

```text
include/stm32_raw_tx_demo.h
stm32_bridge/stm32_raw_tx_demo.c
```

이 데모는 공통 RF preset을 적용한 뒤 433.92 MHz PoC 대역, `CHANNR=0x05`, CC1101 address byte `0x00`, TDMA aircraft/mobile source address `0x31` 기준으로 1초마다 `stm32-rssi:<seq>` TDMA data packet을 송신한다.

## Platform Glue

`Core/Src/main.c`에 다음 include를 추가한다.

```c
/* USER CODE BEGIN Includes */
#include "cc1101_stm32.h"
#include "usb_cdc_bridge.h"
/* USER CODE END Includes */
```

`Core/Src/main.c`의 `USER CODE BEGIN 0`에 platform 함수를 추가한다.

```c
/* USER CODE BEGIN 0 */
extern SPI_HandleTypeDef hspi1;

void cc1101_platform_select(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

void cc1101_platform_deselect(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

void cc1101_platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

int cc1101_platform_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    HAL_StatusTypeDef rc = HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx,
                                                   (uint16_t)len, 100);
    return rc == HAL_OK ? 0 : -1;
}
/* USER CODE END 0 */
```

`main()`에서 peripheral init 이후 bridge init을 호출한다.

```c
/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
usb_cdc_bridge_init();
/* USER CODE END 2 */
```

RSSI logger용 단순 송신 테스트만 먼저 할 때는 `usb_cdc_bridge_init()` 대신 아래처럼 호출한다.

```c
/* USER CODE BEGIN Includes */
#include "stm32_raw_tx_demo.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(CC1101_CSN_GPIO_Port, CC1101_CSN_Pin, GPIO_PIN_SET);
stm32_raw_tx_demo_init();
/* USER CODE END 2 */

/* USER CODE BEGIN WHILE */
while (1)
{
    stm32_raw_tx_demo_tick(HAL_GetTick());

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
```

## USB CDC Receive Hook

CubeMX가 생성한 `USB_DEVICE/App/usbd_cdc_if.c`에서 `CDC_Receive_FS()`를 찾는다.

include 추가:

```c
/* USER CODE BEGIN INCLUDE */
#include "usb_cdc_bridge.h"
/* USER CODE END INCLUDE */
```

`CDC_Receive_FS()` 내부에 다음을 넣는다.

```c
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
    /* USER CODE BEGIN 6 */
    uint8_t out[128];
    int out_len = usb_cdc_bridge_parse(Buf, *Len, out, sizeof(out));
    if (out_len > 0) {
        CDC_Transmit_FS(out, (uint16_t)out_len);
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return (USBD_OK);
    /* USER CODE END 6 */
}
```

## Radio Poll Hook

처음 테스트는 `while (1)`에서 polling으로 충분하다.

`Core/Src/main.c`의 while loop에 추가:

```c
/* USER CODE BEGIN WHILE */
while (1)
{
    uint8_t out[128];
    int out_len = usb_cdc_bridge_poll_radio(out, sizeof(out));
    if (out_len > 0) {
        CDC_Transmit_FS(out, (uint16_t)out_len);
    }
    HAL_Delay(5);

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
```

이 코드를 쓰려면 `main.c`에도 USB CDC transmit 함수 선언이 필요할 수 있다.

```c
#include "usbd_cdc_if.h"
```

## Desktop Test

PC에서 STM32가 `COM3`으로 잡혔다면:

```powershell
cd link16-tdma
cmake -S . -B build
cmake --build build --target tdma_desktop
.\build\Debug\tdma_desktop.exe COM3 100 desktop_master_log.csv
```

MinGW나 Ninja 단일 구성 빌드라면 실행 파일 위치가 다를 수 있다.

```powershell
.\build\tdma_desktop.exe COM3 100 desktop_master_log.csv
```

## Expected Flow

1. Desktop opens COM port.
2. Desktop sends `BRIDGE_CMD_START_RX`.
3. STM32 applies CC1101 RX mode.
4. Desktop sends beacon every frame.
5. STM32 transmits beacon through CC1101.
6. Gateway node receives beacon or packet through CC1101.
7. STM32 polling finds received packet and sends `BRIDGE_EVT_RX_PACKET` to desktop.
8. The RX event payload is `[radio_frame...][rssi][lqi]`.
9. Desktop logs `TX_BEACON`, `RX_OK`, `RX_BAD`, or `TX_FAIL` to CSV.
10. `RX_OK` includes raw RSSI, converted dBm, LQI, and a rough 433 MHz distance estimate.

## Current Hardware-Aligned Firmware Targets

- Board: STM32F401RE
- Radio: CC1101 433 MHz module
- SPI: SPI1 mode 0, MSB first, target 6.5 MHz after bring-up
- CSN: PA4 / D10 GPIO output, idle HIGH
- GDO0: PB0 or actual D2 wiring as GPIO EXTI after polling RX is stable
- TDMA slot: 10.00 ms
- TX/RX window: 8.00 ms
- PLL/state settling: 1.20 ms
- Guard time: 0.80 ms
- Frequency preset: 433.919830 MHz from `FREQ2/1/0 = 0x10/0xb0/0x71`

## First Debug Checklist

- CC1101 registers and power configurations were already confirmed before full-chain testing.
- STM32 CSN idles HIGH.
- SPI mode is 0.
- CC1101 VCC is 3.3V.
- Desktop COM port matches Device Manager.
- If desktop opens COM but no RF activity appears, check `CDC_Receive_FS()` is actually called.
- If RF TX happens but receiver node receives nothing, lower SPI speed and recheck RF preset/channel.

## Standalone Nodes Setup (앵커 및 비행체 단독 노드 설정)

데스크톱 PC 연결 없이 전원만 공급받아 단독으로 작동하는 앵커(Anchor) 및 비행체(Aircraft) 노드는 다음 사항을 다르게 적용합니다.

### 1. CubeMX 설정 차이점
- **USB CDC 비활성화 가능**: PC와의 통신이 필요 없으므로 `USB_OTG_FS` 및 `USB_DEVICE` 설정을 생략하여 전력 및 메모리를 절약할 수 있습니다.
- **GPIO / SPI**: SPI1, CSN(PA4), GDO0(PB0) 등의 핀맵 설정은 브리지 보드와 동일하게 구성해야 합니다.

### 2. 복사해야 할 필수 소스 파일 추가
데스크톱 브리지와 달리 단독 노드는 보드 자체에서 TDMA 슬롯 스케줄링을 연산해야 하므로, 아래 공유 소스 코드를 프로젝트에 **반드시 추가**해야 합니다.
- **Header 파일**: `include/tdma.h`, `include/tdma_runtime.h` 추가 복사
- **Source 파일**: `common/tdma.c`, `common/tdma_runtime.c` 추가 복사

### 3. 메인 동작 루프 (`main.c` 통합)
USB CDC 브리지의 요청 처리 루프 대신, 단독 노드는 고유 타이머를 기반으로 TDMA 상태 머신을 구동해야 합니다. 
이를 위해 `Core/Src/main.c`에 **[stm32_bridge/main_stm32.c](file:///c:/Users/5-13/home-lab/link16-tdma/stm32_bridge/main_stm32.c)**의 메인 루프 시퀀스를 통합하여 사용합니다:
- `get_monotonic_us()` 등의 타이머 함수를 활용해 1us 단위의 시간 값을 획득합니다.
- `tdma_runtime_tick()` 함수를 호출하여 매 순간 송신(TX)과 수신(RX)을 독립적으로 제어합니다.
- `tdma_runtime_init(&tdma_runtime, <Node_Address>)`를 호출할 때 자신의 고유 하드웨어 주소(예: 0x22, 0x31)를 주입합니다.

