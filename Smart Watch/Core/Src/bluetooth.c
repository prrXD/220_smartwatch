#include "bluetooth.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

/* Ring buffer for DMA RX */
static uint8_t rx_buf[BT_RX_BUF_SIZE];
static uint8_t tx_buf[BT_TX_BUF_SIZE];
static uint16_t rx_old_pos = 0;

/* AA frame parser state */
static uint8_t frame_buf[BT_RX_BUF_SIZE];
static uint8_t frame_idx = 0;
static uint8_t frame_expected_len = 0;

/* Time sync state machine (streaming parser, format: THH:MM:SS) */
#define TSS_IDLE    0
#define TSS_HH1     1   /* T received, expecting HH tens digit */
#define TSS_HH2     2   /* expecting HH ones digit */
#define TSS_COL1    3   /* expecting ':' after HH */
#define TSS_MM1     4   /* expecting MM tens digit */
#define TSS_MM2     5   /* expecting MM ones digit */
#define TSS_COL2    6   /* expecting ':' after MM (optional) */
#define TSS_SS1     7   /* expecting SS tens digit */
#define TSS_SS2     8   /* expecting SS ones digit */

static uint8_t ts_state = TSS_IDLE;
static uint8_t ts_h = 0, ts_m = 0, ts_s = 0;

/* Time sync pending flag */
static volatile uint8_t time_sync_pending = 0;
static uint8_t sync_hour = 0, sync_minute = 0, sync_second = 0;

/* ─── Process one byte: streaming THH:MM:SS parse + AA binary frame ─── */
static void BT_ProcessByte(uint8_t byte)
{
    /* (A) Streaming THH:MM:SS parser (matches "T21:42:00" or "T21:42") */
    switch (ts_state)
    {
    case TSS_IDLE:
        ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_HH1:
        if (byte >= '0' && byte <= '9') { ts_h = (byte - '0') * 10; ts_state = TSS_HH2; }
        else ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_HH2:
        if (byte >= '0' && byte <= '9')
        {
            ts_h += (byte - '0');
            ts_state = (ts_h <= 23) ? TSS_COL1 : ((byte == 'T') ? TSS_HH1 : TSS_IDLE);
        }
        else ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_COL1:
        if (byte == ':') ts_state = TSS_MM1;
        else ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_MM1:
        if (byte >= '0' && byte <= '9') { ts_m = (byte - '0') * 10; ts_state = TSS_MM2; }
        else ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_MM2:
        if (byte >= '0' && byte <= '9')
        {
            ts_m += (byte - '0');
            ts_state = (ts_m <= 59) ? TSS_COL2 : ((byte == 'T') ? TSS_HH1 : TSS_IDLE);
        }
        else
        {
            /* No seconds delimiter → T21:42 complete */
            ts_s = 0;
            sync_hour = ts_h; sync_minute = ts_m; sync_second = ts_s;
            time_sync_pending = 1;
            ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        }
        break;
    case TSS_COL2:
        if (byte == ':') ts_state = TSS_SS1;
        else
        {
            /* Extra char after MM:2 → treat as T21:42 complete with no seconds */
            ts_s = 0;
            sync_hour = ts_h; sync_minute = ts_m; sync_second = ts_s;
            time_sync_pending = 1;
            ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        }
        break;
    case TSS_SS1:
        if (byte >= '0' && byte <= '9') { ts_s = (byte - '0') * 10; ts_state = TSS_SS2; }
        else ts_state = (byte == 'T') ? TSS_HH1 : TSS_IDLE;
        break;
    case TSS_SS2:
        ts_state = TSS_IDLE; /* reset for next T frame */
        if (byte >= '0' && byte <= '9')
        {
            ts_s += (byte - '0');
            if (ts_s <= 59)
            {
                sync_hour = ts_h; sync_minute = ts_m; sync_second = ts_s;
                time_sync_pending = 1;
            }
        }
        break;
    }

    /* (B) AA binary frame parser */
    if (frame_idx == 0)
    {
        if (byte == BT_STX) { frame_buf[frame_idx++] = byte; }
        return;
    }
    if (frame_idx == 1)  { frame_buf[frame_idx++] = byte; return; }
    if (frame_idx == 2)  { frame_expected_len = byte; frame_buf[frame_idx++] = byte; return; }
    if (frame_idx < 3 + frame_expected_len) { frame_buf[frame_idx++] = byte; return; }
    if (frame_idx == 3 + frame_expected_len) { frame_buf[frame_idx++] = byte; return; }

    if (byte == BT_ETX)
    {
        uint8_t chk = frame_buf[1] ^ frame_buf[2];
        for (uint8_t j = 0; j < frame_expected_len; j++)
            chk ^= frame_buf[3 + j];
        if (chk == frame_buf[3 + frame_expected_len])
        {
            if (frame_buf[1] == BT_CMD_TIME_SYNC && frame_expected_len == 7)
            {
                sync_hour   = frame_buf[3];
                sync_minute = frame_buf[4];
                sync_second = frame_buf[5];
                time_sync_pending = 1;
            }
        }
    }
    frame_idx = 0;
    frame_expected_len = 0;
}

/* ═══════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════ */

/**
 * @brief 强制复位 HC-05 蓝牙模块
 * @note  HC-05 异常时（指示灯常亮、无法连接）调用。
 *        通过将 TX (PA2) 拉低 30ms 产生 BREAK 条件，促使 HC-05 的
 *        UART 接收状态机复位，然后重新初始化 USART2+DMA。
 *        如果硬件上将 HC-05 的 EN 脚连接到 GPIO，拉低 EN 后再释放效果更可靠。
 */
void BT_ForceReset(void)
{
    /* 停止 USART2 和 DMA */
    HAL_UART_DeInit(&huart2);

    /* 配置 PA2 为 GPIO 推挽输出 */
    GPIOA->CRL = (GPIOA->CRL & ~(0xF << 8)) | (0x3 << 8);

    /* 拉低 PA2 30ms — BREAK 条件 */
    GPIOA->BRR = GPIO_PIN_2;
    HAL_Delay(30);

    /* 恢复高电平，等待 HC-05 内部复位 */
    GPIOA->BSRR = GPIO_PIN_2;
    HAL_Delay(20);

    /* 重新初始化 USART2 及 DMA */
    MX_USART2_UART_Init();
    BT_Init();
}

void BT_Init(void)
{
    /* Start DMA reception on USART2 (Normal mode, IDLE line callback) */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buf, BT_RX_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);

    rx_old_pos = 0;
    frame_idx = 0;
    ts_state = TSS_IDLE;
    time_sync_pending = 0;
}

/* UART RX IDLE callback (called from USART2_IRQHandler → HAL layer) */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2 && Size > 0)
    {
        uint16_t new_pos = Size;
        if (new_pos > BT_RX_BUF_SIZE) new_pos = BT_RX_BUF_SIZE;

        if (new_pos != rx_old_pos)
        {
            if (new_pos > rx_old_pos)
            {
                for (uint16_t i = rx_old_pos; i < new_pos; i++)
                    BT_ProcessByte(rx_buf[i]);
            }
            else
            {
                for (uint16_t i = rx_old_pos; i < BT_RX_BUF_SIZE; i++)
                    BT_ProcessByte(rx_buf[i]);
                for (uint16_t i = 0; i < new_pos; i++)
                    BT_ProcessByte(rx_buf[i]);
            }
            rx_old_pos = new_pos;
        }
    }

    /* Restart DMA (Normal mode stops on IDLE, must restart) */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buf, BT_RX_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
}

/* Check and apply time sync. Returns 1 if time was updated. */
uint8_t BT_Process(SmartWatchData_t *data)
{
    if (time_sync_pending && data != NULL)
    {
        data->hour   = sync_hour;
        data->minute = sync_minute;
        data->second = sync_second;
        time_sync_pending = 0;
        return 1;
    }
    return 0;
}

/* Send sensor data as human-readable text over Bluetooth */
void BT_SendSensorData(SmartWatchData_t *data)
{
    int16_t ax_i = (int16_t)(data->accel.ax * 100.0f);
    int16_t ay_i = (int16_t)(data->accel.ay * 100.0f);
    int16_t az_i = (int16_t)(data->accel.az * 100.0f);
    int16_t gx_i = (int16_t)(data->gyro.gx * 10.0f);
    int16_t gy_i = (int16_t)(data->gyro.gy * 10.0f);
    int16_t gz_i = (int16_t)(data->gyro.gz * 10.0f);
    int16_t p_i  = (int16_t)(data->angle.pitch * 10.0f);
    int16_t r_i  = (int16_t)(data->angle.roll * 10.0f);

    int ax_abs = (ax_i < 0 ? -ax_i : ax_i), ay_abs = (ay_i < 0 ? -ay_i : ay_i),
        az_abs = (az_i < 0 ? -az_i : az_i);
    int gx_abs = (gx_i < 0 ? -gx_i : gx_i), gy_abs = (gy_i < 0 ? -gy_i : gy_i),
        gz_abs = (gz_i < 0 ? -gz_i : gz_i);
    int p_abs = (p_i < 0 ? -p_i : p_i), r_abs = (r_i < 0 ? -r_i : r_i);

    int len = snprintf((char *)tx_buf, BT_TX_BUF_SIZE,
        "%02d:%02d:%02d  ax:%c%d.%02d ay:%c%d.%02d az:%c%d.%02d  gx:%c%d.%d gy:%c%d.%d gz:%c%d.%d  pitch:%c%d.%d roll:%c%d.%d  temp:%d  steps:%lu\r\n",
        data->hour, data->minute, data->second,
        ax_i < 0 ? '-' : '+', ax_abs / 100, ax_abs % 100,
        ay_i < 0 ? '-' : '+', ay_abs / 100, ay_abs % 100,
        az_i < 0 ? '-' : '+', az_abs / 100, az_abs % 100,
        gx_i < 0 ? '-' : '+', gx_abs / 10,  gx_abs % 10,
        gy_i < 0 ? '-' : '+', gy_abs / 10,  gy_abs % 10,
        gz_i < 0 ? '-' : '+', gz_abs / 10,  gz_abs % 10,
        p_i  < 0 ? '-' : '+', p_abs  / 10,  p_abs  % 10,
        r_i  < 0 ? '-' : '+', r_abs  / 10,  r_abs  % 10,
        data->temp_celsius,
        (unsigned long)data->step_count);

    if (len > 0 && len < BT_TX_BUF_SIZE)
    {
        HAL_UART_Transmit(&huart2, tx_buf, (uint16_t)len, 100);
    }
}

uint8_t BT_IsConnected(void)
{
    return (HAL_GPIO_ReadPin(BT_STATE_PORT, BT_STATE_PIN) == GPIO_PIN_SET) ? 1 : 0;
}