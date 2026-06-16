#ifndef TDMA_APP_H
#define TDMA_APP_H

#include <stdint.h>

/**
 * @brief Initialize the TDMA application on the STM32.
 * Call this in main.c inside USER CODE BEGIN 2.
 */
void tdma_app_init(void);

/**
 * @brief Tick the TDMA state machine.
 * Call this in main.c inside the while(1) loop under USER CODE BEGIN 3.
 */
void tdma_app_tick(void);

#endif /* TDMA_APP_H */
