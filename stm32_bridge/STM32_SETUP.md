# STM32 프로젝트 설정 및 이식 가이드 (MDK-ARM Keil / CubeIDE 공용)

이 문서는 `link16-tdma` 소스코드를 STM32 프로젝트에 쉽고 빠르게 이식하고, 주변장치(SPI, USB, Timer, GPIO)를 구성하는 방법을 설명합니다.

---

## 1. STM32CubeMX 핵심 하드웨어 설정 (Checklist)

코드를 자동 생성하기 전, STM32CubeMX에서 아래 항목들이 제대로 설정되었는지 반드시 확인하세요.

### 🟩 1. System Core -> SYS
* **Debug:** `Serial Wire` 선택 (디버깅 및 락 방지 필수)
* **System Wake-up:** 모두 체크 해제 (Disable)
* **Timebase Source:** `SysTick` (기본값)

### 🟩 2. Clock Configuration (클록 트리)
* **USB 클록:** 화면 우측 하단 `To USB (MHz)` 값이 정확히 **`48 MHz`**인지 확인 (가상 COM 포트 안정성에 필수)

### 🟩 3. Connectivity -> SPI1 (CC1101 무선 연결)
* **Mode:** `Full-Duplex Master` 선택
* **NSS:** `Disable` (소프트웨어 제어로 PA4 핀 직접 토글)
* **Baud Rate (통신 속도):** **`5.0 MHz ~ 6.5 MHz`** 사이로 분주비(Prescaler) 조정
  * CC1101의 최대 SPI 클록 속도는 **10 MHz**이므로 이보다 느려야 통신 오류가 발생하지 않습니다.
  * APB2 주파수가 84 MHz일 경우: **Prescaler = 16** (5.25 MHz) 권장.
* **SPI 모드 설정 (SPI Mode 0):**
  * **Clock Polarity (CPOL):** `Low`
  * **Clock Phase (CPHA):** `1 Edge`

### 🟩 4. Connectivity -> USB_OTG_FS & Middleware -> USB_DEVICE
* **USB_OTG_FS Mode:** `Device_Only`
* **USB_DEVICE Class:** `Communication Device Class (Virtual Port Com)` (CDC 가상 COM 포트 설정)

### 🟩 5. System Core -> GPIO (입출력 및 인터럽트 핀 설정)
* **`PA4` (CSN 핀):**
  * **GPIO Output Level:** **`High`** (Active-Low 이므로 대기 상태에서 3.3V 유지 필수)
  * **GPIO Mode:** `Output Push Pull`
  * **User Label:** `CC1101_CSN`
* **`PA8` (상태 LED 핀):**
  * **GPIO Output Level:** `Low`
  * **GPIO Mode:** `Output Push Pull`
  * **User Label:** `Debug LED` (PA5 SPI1_SCK와의 핀 충돌을 피하기 위해 LED 제어선을 PA8로 격리함)
* **`PB0` (GDO0 수신 완료 인터럽트 핀):**
  * **GPIO Mode:** `External Interrupt Mode with Rising/Falling edge trigger detection`
  * **GPIO Pull-up/Pull-down:** `No pull-up and no pull-down` 또는 `Pull-down`
  * **User Label:** `CC1101_GDO0`
* **`PC13` (파란색 사용자 버튼 핀):**
  * **GPIO Mode:** `Input` (또는 EXTI Falling)
  * **GPIO Pull-up/Pull-down:** `No pull-up and no pull-down` (보드 자체 하드웨어 풀업이 있음)
  * **User Label:** `B1`

### 🟩 6. System Core -> NVIC (인터럽트 허용)
* **GPIO EXTI 인터럽트 활성화:** `EXTI Line0 interrupt` 항목에 **`Enabled`** 체크박스가 체크되어 있는지 최종 확인 (PB0 수신 신호를 받기 위해 필수)

### 🟩 7. Timers -> TIM2 (TDMA 마이크로초 타이머)
* **TIM2 Mode:** `Internal Clock`
* **Prescaler (PSC):**
  * 시스템 클록 84 MHz 기준: **`83`** (84분주하여 1 MHz로 동작시킴)
  * 시스템 클록 48 MHz 기준: **`47`**
* **Counter Period (ARR):** **`0xFFFFFFFF`** (32비트 최대값 설정으로 오버플로우 방지)
* **Counter Mode:** `Up`
* **NVIC 설정:** 인터럽트(`TIM2 global interrupt`)는 **비활성화(Disabled)** 상태 유지 (단순 카운터 리딩용으로 사용)

---

## 2. Keil 컴파일러 버전 설정 (필수)

최신 Keil uVision(v5.37+)은 구형 ARM Compiler v5(AC5)를 제공하지 않으므로, 프로젝트가 최신 컴파일러를 사용하도록 설정해야 합니다.

1. Keil 화면 좌측 **Project** 창의 최상단 프로젝트 노드를 마우스 우클릭한 뒤, **`Options for Target`**을 클릭합니다. (혹은 상단 툴바의 마술봉 아이콘 클릭)
2. **`Target`** 탭을 선택합니다.
3. 우측 상단의 **`ARM Compiler:`** 드롭다운 메뉴를 클릭하고, **`Use default compiler version 6`** (혹은 목록에 표시된 최신 `Version 6.xx`)으로 변경한 후 **`OK`**를 누릅니다.

---

## 3. 파일 복사 및 배포 구조 (Drag-and-Drop)

소스코드의 중복 관리를 피하고 배포를 자동화하기 위해, 동적 파일 복사 패키징 스크립트를 제공합니다.

### 1) 패키징 스크립트 실행
터미널에서 프로젝트 루트 디렉터리로 이동한 후 아래 스크립트를 실행합니다.
```powershell
python stm32_bridge/pack_stm32.py
```
실행 완료 시, `stm32_bridge/Inc` 폴더와 `stm32_bridge/Src` 폴더 내에 EKF 알고리즘, TDMA 시간 동기화, CC1101 프리셋 등을 포함한 **모든 필요한 소스 및 헤더 파일이 자동으로 수집**됩니다. 
또한 시스템에 Keil 프로젝트 경로(`C:\Users\devSh\Documents\link16\link16-node`)가 존재할 경우, **해당 Keil 프로젝트의 `Core/Inc` 및 `Core/Src` 폴더로 자동 동기화(Copy)가 수행**되므로 수동 복사가 필요 없습니다.

### 2) 드래그 앤 드롭 파일 복사 (수동 동기화 필요 시)
* 스크립트 자동 동기화 대상 외의 경로에 프로젝트가 있거나 수동 동기화가 필요한 경우:
  * 생성된 `stm32_bridge/Inc` 폴더의 모든 헤더 파일들을 STM32 프로젝트의 **`Core/Inc`** 폴더에 넣습니다.
  * 생성된 `stm32_bridge/Src` 폴더의 모든 소스 파일들을 STM32 프로젝트의 **`Core/Src`** 폴더에 넣습니다.

---

## 3. main.c 연동 코드 (Platform Glue 자동 내장)

모든 드라이버 바인딩 코드(SPI 송수신, CSN 제어, 1us 타이머, LED/버튼 제어)는 `tdma_app.c` 내에 **자동으로 연결**되어 있으므로, 사용자는 CubeMX가 생성한 `Core/Src/main.c` 파일에 **단 2줄의 호출 코드**만 작성하면 연동이 완료됩니다.

### 1) Includes 추가
`Core/Src/main.c`의 `USER CODE BEGIN Includes` 영역에 헤더를 임포트합니다.
```c
/* USER CODE BEGIN Includes */
#include "tdma_app.h"
/* USER CODE END Includes */
```

### 2) 앱 초기화 (Init) 호출
`Core/Src/main.c`의 `main()` 함수 내부에서 주변장치 초기화가 끝난 직후 `tdma_app_init()`을 호출합니다.
```c
  /* USER CODE BEGIN 2 */
  tdma_app_init();
  /* USER CODE END 2 */
```

### 3) 앱 주기적 루프 (Tick) 호출
`Core/Src/main.c`의 `while(1)` 루프 내부 `USER CODE BEGIN 3` 영역에 `tdma_app_tick()`을 대입합니다.
```c
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    tdma_app_tick();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
```

---

## 5. 노드 역할(Master/Anchor/Aircraft) 변경 방법

보드에 굽기 직전, 해당 보드가 마스터 기지국으로 동작할지, 앵커 기지국으로 동작할지, 혹은 비행체 노드로 동작할지는 [tdma_app.c](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c) 파일의 아래 코드 매개변수 하나만 변경하면 즉시 동작 조건이 스위칭됩니다.

* **[tdma_app.c](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c)** 진입점 함수 부근:
  ```c
  /* 아래 세 종류의 주소 상수 중 하나를 기입하여 빌드 후 각 보드에 라이팅합니다. */
  tdma_runtime_init(&tdma_runtime, TDMA_AIRCRAFT_ADDR); // <- 비행체 (0x31)
  // tdma_runtime_init(&tdma_runtime, TDMA_MASTER_ADDR);   // <- 마스터 기지국 (0x21)
  // tdma_runtime_init(&tdma_runtime, TDMA_ANCHOR_1_ADDR); // <- 앵커 기지국 1 (0x22)
  ```

---

## 6. 자주 발생하는 컴파일 에러 및 해결 방법 (Troubleshooting)

### ❌ 1. `Target uses ARM-Compiler 'Default Compiler Version 5' which is not available.`
* **원인:** 프로젝트가 MDK v5 구형 컴파일러로 설정되어 있지만 현재 Keil에 해당 컴파일러가 누락된 경우입니다.
* **해결:** 본 문서 **2. Keil 컴파일러 버전 설정** 절차를 참조하여 컴파일러 설정을 **Version 6**으로 스위칭하세요.

### ❌ 2. `error: use of undeclared identifier 'hspi1' 또는 'htim2'`
* **원인:** `tdma_app.c`에서 사용하는 SPI 및 타이머 핸들이 `main.c`에 선언되어 있지 않거나 CubeMX에서 드라이버를 생성하지 않은 경우입니다.
* **해결:** STM32CubeMX에서 **SPI1**과 **TIM2**가 활성화되고 제너레이트 되었는지 확인해 주세요. 핸들 변수명(`hspi1`, `htim2`)이 다를 경우 `tdma_app.c` 최상단의 `extern` 선언부 이름을 수정해 주어야 합니다.

### ❌ 3. `error: call to undeclared function '__HAL_TIM_GET_COUNTER'`
* **원인:** 프로젝트에 하드웨어 타이머 드라이버 관련 인클루드 또는 매크로가 로드되지 않은 상태입니다.
* **해결:** 최신 소스코드에는 매크로 누락에 대비한 안전 폴백(Fallback)이 내장되어 있습니다. 에러가 발생하면 `tdma_app.c` 최상단에 아래 코드가 들어있는지 확인하세요:
  ```c
  #ifndef __HAL_TIM_GET_COUNTER
  #define __HAL_TIM_GET_COUNTER(__HANDLE__) ((__HANDLE__)->Instance->CNT)
  #endif
  ```

### ❌ 4. `error: use of undeclared identifier 'CC1101_GDO0_Pin'`
* **원인:** CubeMX에서 수신 인터럽트용 GPIO 핀(예: PB0)의 **User Label**을 `CC1101_GDO0`으로 지정하지 않은 상태입니다.
* **해결:** CubeMX로 돌아가 해당 핀의 Label을 `CC1101_GDO0`으로 지정하거나, `tdma_app.c` 최상단에 `#define CC1101_GDO0_Pin GPIO_PIN_0` 처럼 수동 정의를 추가해 주면 해결됩니다.

---

## 7. Keil 32KB 용량 제한 해결을 위한 소스 파일 제외 가이드

Keil uVision 평가판(Evaluation/Lite Edition)의 32KB 코드 크기 제한 에러(`L6047U`)가 발생할 경우, 각 역할 보드 빌드 시 아래 표를 기준으로 불필요한 C 소스 파일들을 빌드 대상에서 제외(`Exclude`)해 주십시오.

### 파일 제외 방법:
- Keil 좌측 Project 트리에서 대상 파일 우클릭 -> **Options for File** 클릭 -> **Include in Target Build** 체크박스 해제.

| 빌드 대상 (Keil Define 매크로) | 빌드에서 제외할 파일 목록 (Exclude 대상 C 파일) |
| :--- | :--- |
| **마스터 브릿지 보드**<br>(`NODE_ROLE_MASTER`) | - `tdma_app.c`<br>- `ekf.c`<br>- `tdma.c`<br>- `tdma_runtime.c`<br>- `node_table.c`<br>- `radio_metrics.c` |
| **비행체 및 앵커 노드 보드**<br>(`NODE_ROLE_AIRCRAFT` 등) | - `usb_cdc_bridge.c`<br>- `ekf.c` (마스터 전용 EKF 알고리즘으로, 비행체/앵커 빌드 시 불필요하여 제외 권장)<br>- `node_table.c` (공용 미사용 소스로, 빌드 시 제외 권장)<br>- **ST USB 라이브러리 관련 파일 일체** (`usb_device.c`, `usbd_cdc_if.c`, `usbd_conf.c`, `usbd_cdc.c`, `usbd_core.c`, `usbd_ctlreq.c`, `usbd_ioreq.c` 등) |

---

## 8. DMA 및 비동기 다중 통신 구현 수칙 (체크리스트)

정밀 TDMA 타이밍 환경에서 DMA와 비동기 인터럽트 통신을 안정적으로 구현하기 위해 프로젝트 전반에 걸쳐 아래 핵심 수칙을 준수해야 합니다.

### 🟩 1. 변수 및 버퍼 선언 지침
* **volatile 키워드 필수 사용:** DMA 완료 콜백 함수와 메인 루프 간 공유되는 모든 플래그/상태 변수에는 컴파일러 최적화 방지를 위해 `volatile`을 필수 사용합니다.
  ```c
  volatile uint8_t g_spi_tx_complete = 0;
  volatile uint8_t g_uart_idle_flag = 0;
  ```
* **4바이트 메모리 정렬(Memory Alignment) 명시:** DMA 전송 시 메모리 주소 정렬 어긋남 에러를 막기 위해 통신 버퍼 배열에 4바이트 정렬을 명시합니다.
  ```c
  __attribute__((aligned(4))) uint8_t g_uart_rx_buffer[256];
  ```

### 🟩 2. 하드웨어 자원 및 인터럽트(NVIC) 설정 지침
* **인터럽트 우선순위 계층화:** 다중 통신 충돌 시 정밀 타이밍(TDMA 슬롯)을 보장하기 위해 데이터 속도가 빠르고 실시간성이 높은 주변장치의 우선순위를 가장 높게(Preemption Priority 숫자를 작게) 설정합니다.
  - **1순위 (최상위):** SPI Global Interrupt (예: Preemption Priority = 2)
  - **2순위 (하위):** UART/USART Global Interrupt (예: Preemption Priority = 5)
* **UART 수신 모드:** 데스크톱과의 가상 COM 포트(USART2) 연결 시, CPU 부하를 경감하기 위해 **DMA Circular 모드** 및 **IDLE Line 인터럽트** 조합 사용을 강력히 권장합니다.

### 🟩 3. 데이터 경쟁(Race Condition) 방지 및 자원 격리
* **독점권(Lock) 보장:** DMA가 메모리를 쓰고 있는 동안 CPU가 동일한 버퍼 영역을 직접 쓰거나 읽지 않도록 플래그를 제어해야 합니다.
* **역할 분담 (인터럽트 최소화):** DMA/GPIO 콜백(ISR) 내부에서는 복잡한 패킷 파싱(Parsing)이나 딜레이 연산을 절대로 수행하지 않습니다. 콜백에서는 플래그 세팅 또는 간단한 복사만 수행하고, 실제 디코딩은 메인 루프에서 처리합니다.
  - ex) [HAL_GPIO_EXTI_Callback](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c#L99) 내부에서는 플래그만 전환하고, 메인 루프 [tdma_app_tick](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c#L692)에서 패킷 처리 호출.
* **SPI 버스 동기화:** DMA 전송이 백그라운드에서 진행 중일 때는 동일한 SPI 버스를 사용하는 다른 주변장치/레지스터 접근을 소프트웨어 락(Mutex) 등으로 완전히 차단해야 합니다.

### 🟩 4. 안전장치 및 버퍼 오버플로우 방지
* **하드코딩 금지:** `HAL_UART_Receive_DMA`, `HAL_SPI_TransmitReceive_DMA` 등 크기 지정 매개변수 호출 시 버퍼 크기 불일치 오염을 방지하기 위해 `sizeof()` 연산자를 명시합니다.
* **더블 버퍼링(Ping-Pong Buffer) 구조 검토:** 수신 데이터 유입 빈도가 매우 높은 환경에서는 `HAL_UARTEx_ReceiveToIdle_DMA` 등을 이용한 핑퐁 버퍼링 기법을 적용합니다.
* **DMA 에러 복구 콜백 등록:** DMA 오류로 채널이 락업되는 것을 방지하기 위해 `HAL_UART_ErrorCallback` 및 `HAL_SPI_ErrorCallback`을 구현하여 비상 시 채널을 자동 재부팅(Flush & Restart)합니다.
* **임계 구역(Critical Section) 최소화:** 지터 방지를 위해 인터럽트 완전 차단(`__disable_irq()`) 구간은 10us 이내로 최소화합니다.


