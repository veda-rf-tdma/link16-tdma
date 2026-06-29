#include "usb_cdc_bridge.h"
#include "cc1101_stm32.h"
#include "protocol.h"
#include "config.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define CC1101_READ 0x80u
#define CC1101_BURST 0x40u
#define CC1101_STATUS_MARCSTATE 0x35u

static volatile uint8_t g_handshake_done = 0;
static uint8_t rx_buf[128];
static volatile int rx_idx = 0;

#define UART_RX_DMA_BUF_SIZE 256
__attribute__((aligned(4))) static uint8_t g_uart_rx_dma_buf[UART_RX_DMA_BUF_SIZE];
__attribute__((aligned(4))) static uint8_t g_uart_tx_dma_buf[256];

volatile uint8_t g_uart_tx_complete = 1;
volatile uint8_t g_uart_rx_ready = 0;
static uint16_t last_rx_pos = 0;

int usb_cdc_bridge_send_dma(const uint8_t *data, uint16_t len);

enum {
    BRIDGE_MAGIC = 0xa5u,
    BRIDGE_MAX_FRAME = 128u
};

static cc1101_t bridge_radio;

static size_t bridge_build_frame(uint8_t type, const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t out_len)
{
    if (payload_len > 255u || out_len < payload_len + 5u) {
        return 0;
    }
    out[0] = BRIDGE_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)payload_len;
    for (size_t i = 0; i < payload_len; i++) {
        out[3 + i] = payload[i];
    }
    uint16_t crc = tdma_crc16_ccitt(out, payload_len + 3u);
    out[3 + payload_len] = (uint8_t)(crc >> 8);
    out[4 + payload_len] = (uint8_t)(crc & 0xff);
    return payload_len + 5u;
}

static int bridge_validate_frame(const uint8_t *in, size_t in_len)
{
    if (!in || in_len < 5 || in[0] != BRIDGE_MAGIC) {
        return -1;
    }
    size_t payload_len = in[2];
    if (in_len != payload_len + 5u) {
        return -2;
    }
    uint16_t expected = (uint16_t)(((uint16_t)in[3 + payload_len] << 8) |
                                   in[4 + payload_len]);
    uint16_t actual = tdma_crc16_ccitt(in, payload_len + 3u);
    return expected == actual ? 0 : -3;
}

static int bridge_error(uint8_t code, uint8_t *out, size_t out_len)
{
    return (int)bridge_build_frame(BRIDGE_EVT_ERROR, &code, 1, out, out_len);
}

int usb_cdc_bridge_send_dma(const uint8_t *data, uint16_t len)
{
    extern UART_HandleTypeDef huart2;
    uint32_t timeout = 500000;
    while (!g_uart_tx_complete && --timeout) {
        __asm("NOP");
    }
    if (len > sizeof(g_uart_tx_dma_buf)) {
        return -1;
    }
    g_uart_tx_complete = 0;
    memcpy(g_uart_tx_dma_buf, data, len);
    if (HAL_UART_Transmit_DMA(&huart2, g_uart_tx_dma_buf, len) != HAL_OK) {
        g_uart_tx_complete = 1;
        return -1;
    }
    return 0;
}

int usb_cdc_bridge_init(void)
{
    extern UART_HandleTypeDef huart2;

    // Enable USART2 interrupts
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    int rc = cc1101_apply_rf_preset(&bridge_radio);
    if (rc >= 0) {
        cc1101_set_tx_power(&bridge_radio, CC1101_TX_POWER_DBM);
        /* Turn on Debug LED (LD2 on Nucleo board) to show successful startup */
        HAL_GPIO_WritePin(Debug_LED_GPIO_Port, Debug_LED_Pin, GPIO_PIN_SET);
        
        // Start UART Receive DMA with IDLE detection
        last_rx_pos = 0;
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, g_uart_rx_dma_buf, UART_RX_DMA_BUF_SIZE);
        
        usb_cdc_bridge_send_dma((const uint8_t *)"MASTER_BOOT_OK\r\n", 16);
    } else {
        usb_cdc_bridge_send_dma((const uint8_t *)"ERROR: Master CC1101 init failed\r\n", 34);
    }
    return rc;
}

int usb_cdc_bridge_parse(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    int rc = bridge_validate_frame(in, in_len);
    if (rc != 0) {
        return bridge_error((uint8_t)(0x10u - rc), out, out_len);
    }

    uint8_t type = in[1];
    const uint8_t *payload = &in[3];
    size_t payload_len = in[2];
    switch (type) {
    case BRIDGE_CMD_START_RX:
        rc = cc1101_start_rx(&bridge_radio);
        if (rc >= 0) {
            g_handshake_done = 1;
        }
        return rc < 0 ? bridge_error(0x21, out, out_len) :
                        (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, 0, 0, out, out_len);
    case BRIDGE_CMD_TX_PACKET:
        rc = cc1101_send_packet(&bridge_radio, payload, payload_len);
        return rc < 0 ? bridge_error(0x22, out, out_len) :
                        (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, 0, 0, out, out_len);
    case BRIDGE_CMD_GET_STATUS: {
        uint8_t status = bridge_radio.initialized;
        return (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, &status, 1, out, out_len);
    }
    default:
        return bridge_error(0x20, out, out_len);
    }
}

int usb_cdc_bridge_poll_radio(uint8_t *out, size_t out_len)
{
    __attribute__((aligned(4))) uint8_t radio_frame[96];
    uint8_t rssi = 0;
    uint8_t lqi = 0;
    int n = cc1101_poll_packet(&bridge_radio, radio_frame, sizeof(radio_frame), &rssi, &lqi);
    if (n < 0) {
        static char err_log[64];
        int err_len = snprintf(err_log, sizeof(err_log), "INFO: Master poll error %d\r\n", n);
        usb_cdc_bridge_send_dma((const uint8_t *)err_log, (uint16_t)err_len);
        return 0;
    }
    if (n == 0) {
        return 0;
    }

    /* Print a debug telemetry log when Master bridge receives a packet over RF */
    extern UART_HandleTypeDef huart2;
    static char rx_log[256];
    tdma_packet_t packet;
    /* radio_frame[0] is the CC1101 destination address; TDMA payload starts at radio_frame[1] */
    int rc = tdma_decode_payload(radio_frame + 1, (size_t)(n - 1), &packet);
    
    static char hex_str[120];
    memset(hex_str, 0, sizeof(hex_str));
    for (int i = 0; i < n && i < 38; i++) {
        snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str), "%02X ", radio_frame[i]);
    }
    
    if (rc == 0) {
        snprintf(rx_log, sizeof(rx_log), "INFO: Master RX OK | Src=0x%02X | Type=%u | Frame=%u | RSSI_RAW=0x%02X | LQI=0x%02X | Hex: %s\r\n",
                 packet.src, packet.type, packet.frame_no, rssi, lqi, hex_str);
    } else {
        snprintf(rx_log, sizeof(rx_log), "INFO: Master RX BAD | Len=%d | rc=%d | RSSI_RAW=0x%02X | LQI=0x%02X | Hex: %s\r\n",
                 n, rc, rssi, lqi, hex_str);
    }
    /* Commented out debug print to prevent UART DMA collision with bridge frame transmission */
    /* usb_cdc_bridge_send_dma((const uint8_t *)rx_log, (uint16_t)strlen(rx_log)); */

    uint8_t payload[100];
    if ((size_t)n + 2u > sizeof(payload)) {
        return bridge_error(0x30, out, out_len);
    }
    for (int i = 0; i < n; i++) {
        payload[i] = radio_frame[i];
    }
    payload[n] = rssi;
    payload[n + 1] = lqi;
    return (int)bridge_build_frame(BRIDGE_EVT_RX_PACKET, payload, (size_t)n + 2u,
                                   out, out_len);
}

void usb_cdc_bridge_tick(void)
{
    extern UART_HandleTypeDef huart2;
    
#if defined(RF_RAW_TEST_MODE) && (RF_RAW_TEST_MODE == 1u)
    /* Pure Hardware Test Mode - Master Node Tx loop */
    static uint32_t last_tx_ms = 0;
    uint32_t now_ms = HAL_GetTick();
    if (now_ms - last_tx_ms >= 1000) {
        last_tx_ms = now_ms;
        
        /* Construct a real TDMA Beacon packet */
        tdma_packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.network_id = TDMA_NETWORK_ID;
        packet.type = TDMA_PKT_BEACON;
        packet.src = TDMA_MASTER_ADDR;
        packet.dst = TDMA_ADDR_BROADCAST;
        static uint16_t test_frame_no = 0;
        packet.frame_no = test_frame_no++;
        packet.slot_no = 0;
        
        tdma_beacon_payload_t beacon_payload;
        beacon_payload.frame_no = packet.frame_no;
        beacon_payload.frame_period_us = 100000u;
        beacon_payload.slot_us = 10000u;
        beacon_payload.guard_us = 800u;
        beacon_payload.slot_table_version = 0x0Fu;
        beacon_payload.aircraft_x_cm = 123;
        beacon_payload.aircraft_y_cm = 456;
        
        size_t payload_len = tdma_encode_beacon(&beacon_payload, packet.payload, sizeof(packet.payload));
        packet.payload_len = (uint8_t)payload_len;
        
        uint8_t tdma_payload[80];
        size_t tx_payload_len = tdma_encode_payload(&packet, tdma_payload, sizeof(tdma_payload));
        
        uint8_t tx_buf[96];
        size_t tx_len = cc1101_wrap_variable_packet(0x00, tdma_payload, tx_payload_len, tx_buf, sizeof(tx_buf));
        
        /* Set channel and send packet over the air */
        cc1101_set_channel(&bridge_radio, FHSS_CHANNEL_BASE);
        cc1101_send_packet(&bridge_radio, tx_buf, tx_len);
        
        char tx_log[256];
        int tx_offset = snprintf(tx_log, sizeof(tx_log), "[TX Test] Sent TDMA Beacon (frame=%d, len=%d):", (int)packet.frame_no, (int)tx_len);
        for (size_t i = 0; i < tx_len && tx_offset < (int)sizeof(tx_log) - 10; i++) {
            tx_offset += snprintf(tx_log + tx_offset, sizeof(tx_log) - tx_offset, " %02X", tx_buf[i]);
        }
        snprintf(tx_log + tx_offset, sizeof(tx_log) - tx_offset, "\r\n");
        usb_cdc_bridge_send_dma((const uint8_t *)tx_log, (uint16_t)strlen(tx_log));
    }
    return;
#endif

    /* Heartbeat LED Toggle to verify if loop is running */
    static uint32_t hb_cnt = 0;
    if (++hb_cnt >= 200000) {
        HAL_GPIO_TogglePin(Debug_LED_GPIO_Port, Debug_LED_Pin);
        hb_cnt = 0;
    }

    // Auto-restart DMA if stopped or not initialized
    if (huart2.RxState == HAL_UART_STATE_READY) {
        last_rx_pos = 0;
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, g_uart_rx_dma_buf, UART_RX_DMA_BUF_SIZE);
    }
    
    while (1) {
        int rx_len_snap;
        __disable_irq();
        rx_len_snap = rx_idx;
        __enable_irq();
        
        if (rx_len_snap < 1) {
            break;
        }
        
        int start = -1;
        __disable_irq();
        for (int i = 0; i < rx_idx; i++) {
            if (rx_buf[i] == 0xA5) {
                start = i;
                break;
            }
        }
        __enable_irq();
        
        if (start < 0) {
            __disable_irq();
            rx_idx = 0;
            __enable_irq();
            break;
        }
        
        if (start > 0) {
            __disable_irq();
            memmove(rx_buf, rx_buf + start, rx_idx - start);
            rx_idx -= start;
            __enable_irq();
        }
        
        int snap_idx;
        __disable_irq();
        snap_idx = rx_idx;
        __enable_irq();
        if (snap_idx < 3) {
            break;
        }
        
        uint8_t type = rx_buf[1];
        uint8_t payload_len = rx_buf[2];
        uint32_t expected_len = (uint32_t)payload_len + 5u;
        
        // Sane check: type must be a valid bridge command, and length must fit in max frame.
        if (type < 1 || type > 4 || expected_len > 128) {
            __disable_irq();
            memmove(rx_buf, rx_buf + 1, rx_idx - 1);
            rx_idx -= 1;
            __enable_irq();
            continue;
        }
        
        int snap_idx2;
        __disable_irq();
        snap_idx2 = rx_idx;
        __enable_irq();
        if (snap_idx2 < expected_len) {
            break; // Wait for the remaining bytes
        }
        
        // Full expected frame is in buffer. Verify CRC.
        int rc = bridge_validate_frame(rx_buf, expected_len);
        if (rc == 0) {
            uint8_t response[128];
            int resp_len = usb_cdc_bridge_parse(rx_buf, expected_len, response, sizeof(response));
            if (resp_len > 0) {
                usb_cdc_bridge_send_dma(response, (uint16_t)resp_len);
            }
            __disable_irq();
            memmove(rx_buf, rx_buf + expected_len, rx_idx - expected_len);
            rx_idx -= expected_len;
            __enable_irq();
        } else {
            // CRC mismatch. The A5 byte was a false start. Discard 1 byte and retry.
            __disable_irq();
            memmove(rx_buf, rx_buf + 1, rx_idx - 1);
            rx_idx -= 1;
            __enable_irq();
        }
    }
    
    static uint32_t last_poll_ms = 0;
    uint32_t now_ms = HAL_GetTick();
    if (now_ms - last_poll_ms >= 2) {
        last_poll_ms = now_ms;
        uint8_t tx_buf[128];
        int tx_len = usb_cdc_bridge_poll_radio(tx_buf, sizeof(tx_buf));
        if (tx_len > 0) {
            usb_cdc_bridge_send_dma(tx_buf, (uint16_t)tx_len); 
        }
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        uint16_t len = 0;
        if (Size > last_rx_pos) {
            len = Size - last_rx_pos;
            for (uint16_t i = 0; i < len; i++) {
                rx_buf[rx_idx] = g_uart_rx_dma_buf[last_rx_pos + i];
                rx_idx++;
                if (rx_idx >= sizeof(rx_buf)) rx_idx = 0;
            }
        } else if (Size < last_rx_pos) {
            len = UART_RX_DMA_BUF_SIZE - last_rx_pos;
            for (uint16_t i = 0; i < len; i++) {
                rx_buf[rx_idx] = g_uart_rx_dma_buf[last_rx_pos + i];
                rx_idx++;
                if (rx_idx >= sizeof(rx_buf)) rx_idx = 0;
            }
            len = Size;
            for (uint16_t i = 0; i < len; i++) {
                rx_buf[rx_idx] = g_uart_rx_dma_buf[i];
                rx_idx++;
                if (rx_idx >= sizeof(rx_buf)) rx_idx = 0;
            }
        }
        last_rx_pos = Size;
        if (last_rx_pos >= UART_RX_DMA_BUF_SIZE) {
            last_rx_pos = 0;
        }
        g_uart_rx_ready = 1;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        g_uart_tx_complete = 1;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        g_uart_tx_complete = 1;
        HAL_UART_DMAStop(huart);
        last_rx_pos = 0;
        HAL_UARTEx_ReceiveToIdle_DMA(huart, g_uart_rx_dma_buf, UART_RX_DMA_BUF_SIZE);
    }
}

/* 
 * USART2 RX DMA and Idle line detection requires the USART2 global interrupt.
 * We define it here as a strong symbol to override the weak startup vector.
 * If you enable "USART2 global interrupt" in CubeMX NVIC settings later,
 * it will generate duplicate symbol USART2_IRQHandler in stm32f4xx_it.c,
 * in which case you can comment out the definition below.
 */
void USART2_IRQHandler(void)
{
    extern UART_HandleTypeDef huart2;
    HAL_UART_IRQHandler(&huart2);
}
