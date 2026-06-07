#ifndef STM32_RAW_TX_DEMO_H
#define STM32_RAW_TX_DEMO_H

#include <stdint.h>

int stm32_raw_tx_demo_init(void);
int stm32_raw_tx_demo_tick(uint32_t now_ms);

#endif
