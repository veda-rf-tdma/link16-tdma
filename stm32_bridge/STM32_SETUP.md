# STM32 Bridge Setup

이 문서는 `Desktop master -> STM32 USB CDC bridge -> CC1101 -> Raspberry gateway` 테스트를 위해 STM32CubeIDE 프로젝트에 어떤 설정과 코드를 넣어야 하는지 정리한다.

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

## CubeMX Settings

### SYS

- Debug: Serial Wire

	### Clock
	
	- 기본 HSI/PLL 설정으로 시작 가능
	- USB CDC 사용 시 USB clock이 48 MHz 조건을 만족해야 한다.

### SPI1

- Mode: Full-Duplex Master
- Data Size: 8 Bits
- First Bit: MSB First
- Clock Polarity: Low
- Clock Phase: 1 Edge
- NSS: Software
- Prescaler: bring-up은 1 MHz 근처에서 시작하고, 안정화 후 현재 목표 설정인 6.5 MHz로 올린다.

CC1101은 SPI mode 0 기준이다.

### USB

- USB_OTG_FS: Device Only
- Middleware: USB_DEVICE
- Class: Communication Device Class, CDC

### GPIO

- `PA4`: GPIO Output, default HIGH
- `PB0` 또는 실제 GDO0 핀: GPIO_EXTI Rising/Falling, Pull-down 또는 No pull

처음 bring-up에서는 GDO0 EXTI 없이 polling만으로도 테스트 가능하다. EXTI는 RX 안정화 이후 붙인다.

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
6. Raspberry receives beacon or packet through CC1101.
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

- `PARTNUM=0x00`, `VERSION=0x14` was already confirmed on Raspberry side before full-chain testing.
- STM32 CSN idles HIGH.
- SPI mode is 0.
- CC1101 VCC is 3.3V.
- Desktop COM port matches Device Manager.
- If desktop opens COM but no RF activity appears, check `CDC_Receive_FS()` is actually called.
- If RF TX happens but Raspberry receives nothing, lower SPI speed and recheck RF preset/channel.
