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

## 2. 파일 복사 및 배포 구조 (Drag-and-Drop)

소스코드의 중복 관리를 피하고 배포를 자동화하기 위해, 동적 파일 복사 패키징 스크립트를 제공합니다.

### 1) 패키징 스크립트 실행
터미널에서 프로젝트 루트 디렉터리로 이동한 후 아래 스크립트를 실행합니다.
```powershell
python stm32_bridge/pack_stm32.py
```
실행 완료 시, `stm32_bridge/Inc` 폴더와 `stm32_bridge/Src` 폴더 내에 EKF 알고리즘, TDMA 시간 동기화, CC1101 프리셋 등을 포함한 **모든 필요한 소스 및 헤더 파일이 자동으로 수집**됩니다.

### 2) 드래그 앤 드롭 파일 복사
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

## 4. 노드 역할(Master/Anchor/Aircraft) 변경 방법

보드에 굽기 직전, 해당 보드가 마스터 기지국으로 동작할지, 앵커 기지국으로 동작할지, 혹은 비행체 노드로 동작할지는 [tdma_app.c](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c) 파일의 아래 코드 매개변수 하나만 변경하면 즉시 동작 조건이 스위칭됩니다.

* **[tdma_app.c: L269](file:///c:/Users/devSh/Documents/home-lab/link16-tdma/stm32_bridge/Src/tdma_app.c#L269)** 부근:
  ```c
  /* 아래 세 종류의 주소 상수 중 하나를 기입하여 빌드 후 각 보드에 라이팅합니다. */
  tdma_runtime_init(&tdma_runtime, TDMA_AIRCRAFT_ADDR); // <- 비행체 (0x31)
  // tdma_runtime_init(&tdma_runtime, TDMA_MASTER_ADDR);   // <- 마스터 기지국 (0x21)
  // tdma_runtime_init(&tdma_runtime, TDMA_ANCHOR_1_ADDR); // <- 앵커 기지국 1 (0x22)
  ```
