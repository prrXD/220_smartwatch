/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "smartwatch_ui.h"
#include "mpu6050.h"
#include "bluetooth.h"
#include "tim.h"
#include "gpio.h"
#include "i2c.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId taskTimeKeepHandle;
osThreadId taskSensorHandle;
osThreadId taskDisplayHandle;
osThreadId taskBluetoothHandle;
osMutexId mutexI2CHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void vTask_TimeKeep(void const * argument);
void vTask_Sensor(void const * argument);
void vTask_Display(void const * argument);
void vTask_Bluetooth(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* definition and creation of mutexI2C */
  osMutexDef(mutexI2C);
  mutexI2CHandle = osMutexCreate(osMutex(mutexI2C));

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of taskTimeKeep */
  osThreadDef(taskTimeKeep, vTask_TimeKeep, osPriorityHigh, 0, 256);
  taskTimeKeepHandle = osThreadCreate(osThread(taskTimeKeep), NULL);

  /* definition and creation of taskSensor */
  osThreadDef(taskSensor, vTask_Sensor, osPriorityAboveNormal, 0, 512);
  taskSensorHandle = osThreadCreate(osThread(taskSensor), NULL);

  /* definition and creation of taskDisplay */
  osThreadDef(taskDisplay, vTask_Display, osPriorityNormal, 0, 1024);
  taskDisplayHandle = osThreadCreate(osThread(taskDisplay), NULL);

  /* definition and creation of taskBluetooth */
  osThreadDef(taskBluetooth, vTask_Bluetooth, osPriorityBelowNormal, 0, 512);
  taskBluetoothHandle = osThreadCreate(osThread(taskBluetooth), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_vTask_TimeKeep */
/**
  * @brief  Function implementing the taskTimeKeep thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_vTask_TimeKeep */
void vTask_TimeKeep(void const * argument)
{
  /* USER CODE BEGIN vTask_TimeKeep */
  /* Extern: shared data and current page */
  extern SmartWatchData_t watch_data;
  extern osMutexId mutexI2CHandle;

  for (;;)
  {
    /* Time increment does not need I2C mutex - only touches RAM */
    watch_data.second++;
    if (watch_data.second >= 60) { watch_data.second = 0; watch_data.minute++; }
    if (watch_data.minute >= 60) { watch_data.minute = 0; watch_data.hour++; }
    if (watch_data.hour >= 24)   { watch_data.hour = 0; }
    osDelay(1000);
  }
  /* USER CODE END vTask_TimeKeep */
}

/* USER CODE BEGIN Header_vTask_Sensor */
/**
* @brief Function implementing the taskSensor thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vTask_Sensor */
void vTask_Sensor(void const * argument)
{
  /* USER CODE BEGIN vTask_Sensor */
  extern SmartWatchData_t watch_data;
  extern osMutexId mutexI2CHandle;

  for (;;)
  {
    if (watch_data.imu_status)
    {
      if (xSemaphoreTake(mutexI2CHandle, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        MPU6050_ReadAccel(&watch_data.accel);
        MPU6050_ReadGyro(&watch_data.gyro);
        float temp = MPU6050_ReadTemp();
        MPU6050_CalcAngle(&watch_data.accel, &watch_data.angle);
        watch_data.temp_celsius = (int8_t)temp;

        /* 检测传感器是否需要恢复:
           1) I2C 通信错误（HAL BUSY 标志卡死）
           2) MPU6050 内部复位进入 Sleep 模式（所有数据寄存器输出全零:
              加速度 ≈ 0 m/s², 温度 ≈ 36°C） */
        uint8_t need_recovery = 0;

        if (g_mpu6050_i2c_error) {
            g_mpu6050_i2c_error = 0;
            need_recovery = 1;
        } else {
            /* 检测全零输出特征: 加速度幅值平方 < 1.0 且温度 <= 37°C */
            float mag_sq = watch_data.accel.ax * watch_data.accel.ax +
                           watch_data.accel.ay * watch_data.accel.ay +
                           watch_data.accel.az * watch_data.accel.az;
            if (mag_sq < 1.0f && watch_data.temp_celsius <= 37) {
                need_recovery = 1;
            }
        }

        if (need_recovery) {
            HAL_I2C_DeInit(&hi2c1);                 /* 复位 I2C 外设 */
            MX_I2C1_Init();                          /* 重新初始化 I2C（GPIO + 外设寄存器） */
            osDelay(10);                             /* 等待 MPU6050 稳定 */
            if (MPU6050_Init() != 0) {              /* 重新初始化 MPU6050（唤醒+配置） */
                watch_data.imu_status = 0;          /* 失败→进入重试分支 */
            }
        } else {
            /* 传感器数据有效 → 正常更新计步器 */
            if (!watch_data.pedo_paused) {
                MPU6050_Pedometer_Update(&watch_data.accel);
                watch_data.step_count = MPU6050_Pedometer_GetSteps();
            }
        }

        xSemaphoreGive(mutexI2CHandle);
      }
    }
    else
    {
      static uint32_t retry_count = 0;
      if (++retry_count >= 15)
      {
        retry_count = 0;
        if (xSemaphoreTake(mutexI2CHandle, pdMS_TO_TICKS(100)) == pdTRUE)
        {
          if (MPU6050_Init() == 0) watch_data.imu_status = 1;
          xSemaphoreGive(mutexI2CHandle);
        }
      }
    }
    osDelay(200);
  }
  /* USER CODE END vTask_Sensor */
}

/* USER CODE BEGIN Header_vTask_Display */
/**
* @brief Function implementing the taskDisplay thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vTask_Display */
void vTask_Display(void const * argument)
{
  /* USER CODE BEGIN vTask_Display */
  extern SmartWatchData_t watch_data;
  extern osMutexId mutexI2CHandle;
  extern TIM_HandleTypeDef htim3;

  static int32_t  last_cnt = 0;
  static uint32_t btn_down_tick = 0;
  static uint32_t btn2_down_tick = 0;

  for (;;)
  {
    /* ── Read encoder (TIM3, x4 mode) ── */
    int32_t cnt = (int32_t)(int16_t)__HAL_TIM_GET_COUNTER(&htim3);
    int32_t delta = cnt - last_cnt;
    int32_t detents = delta / 4;

    /* ── Read button (PB10, active low, pull-up) ── */
    uint8_t btn = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_RESET) ? 1 : 0;

    /* ── Button handling ── */
    if (btn && btn_down_tick == 0) {
      btn_down_tick = osKernelSysTick();
      osDelay(30);
      if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) != GPIO_PIN_RESET)
        btn_down_tick = 0;
    } else if (!btn && btn_down_tick != 0) {
      uint32_t dur = osKernelSysTick() - btn_down_tick;
      btn_down_tick = 0;
      if (dur >= 50 && dur < 500)
        Menu_HandleOp(MENU_OP_BTN_SHORT);
      else if (dur >= 500)
        Menu_HandleOp(MENU_OP_BTN_LONG);
    }

    /* ── Read button 2 (PA0, active low, pull-up) ── */
    uint8_t btn2 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;

    /* ── Button 2 handling: pedometer pause/resume toggle ── */
    if (btn2 && btn2_down_tick == 0) {
      btn2_down_tick = osKernelSysTick();
      osDelay(30);
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) != GPIO_PIN_RESET)
        btn2_down_tick = 0;
    } else if (!btn2 && btn2_down_tick != 0) {
      uint32_t dur = osKernelSysTick() - btn2_down_tick;
      btn2_down_tick = 0;
      if (dur >= 50 && dur < 500) {
        if (g_app_page == APP_PEDOMETER) {
          watch_data.pedo_paused = !watch_data.pedo_paused;
        }
      }
    }

    /* ── Encoder rotation ── */
    if (detents != 0) {
      last_cnt += detents * 4;
      while (detents > 0) { detents--; Menu_HandleOp(MENU_OP_ROTATE_CW); }
      while (detents < 0) { detents++; Menu_HandleOp(MENU_OP_ROTATE_CCW); }
    }

    /* ── Render ── */
    if (xSemaphoreTake(mutexI2CHandle, 5) == pdTRUE) {
      UI_DrawMenu(&watch_data);
      xSemaphoreGive(mutexI2CHandle);
    }

    osDelay(20);
  }
  /* USER CODE END vTask_Display */
}

/* USER CODE BEGIN Header_vTask_Bluetooth */
/**
* @brief Function implementing the taskBluetooth thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vTask_Bluetooth */
void vTask_Bluetooth(void const * argument)
{
  /* USER CODE BEGIN vTask_Bluetooth */
  extern SmartWatchData_t watch_data;

  TickType_t last_send = 0;
  uint8_t    bt_last_state = 0;       /* 上一周期的 STATE 引脚值 */
  uint16_t   bt_stuck_count = 0;      /* STATE 保持不变的连续轮次（每轮 3s） */
  uint8_t    bt_sync_received = 0;    /* 本次 STATE=HIGH 周期内是否收到过时间同步 */
  uint8_t    bt_recovery_done = 0;    /* 恢复动作去重标记 */

  for (;;)
  {
    /* 检查时间同步 */
    if (BT_Process(&watch_data)) {
        bt_sync_received = 1;         /* 收到手机时间同步 → 连接真实有效 */
    }

    /* 读取 STATE 引脚 */
    uint8_t connected = BT_IsConnected();

    /* ── STATE 变化检测 ── */
    if (connected != bt_last_state) {
        bt_last_state = connected;
        bt_stuck_count = 0;
        bt_sync_received = 0;
        bt_recovery_done = 0;
    }

    watch_data.bt_connected = connected;

    /* 每 3 秒发送传感器数据 */
    TickType_t now = xTaskGetTickCount();
    if ((now - last_send) >= pdMS_TO_TICKS(3000))
    {
      BT_SendSensorData(&watch_data);
      last_send = now;

      /* ── HC-05 异常检测 ──
         条件: STATE 连续 20 轮 (60s) 未变化, 且符合以下任一项:
         1) STATE=HIGH 但从未收到时间同步 → 虚假连接（故障态）
         2) STATE=LOW 持续超过 60s → 可能卡死 */
      bt_stuck_count++;

      if (bt_stuck_count >= 20 && !bt_recovery_done) {
          uint8_t do_recovery = 0;
          if (connected && !bt_sync_received) {
              /* STATE 一直 HIGH 但从未收到数据 → 虚假连接 */
              do_recovery = 1;
          } else if (!connected) {
              /* STATE 一直 LOW 超过 60s → 尝试恢复 */
              do_recovery = 1;
          }
          if (do_recovery) {
              bt_recovery_done = 1;
              BT_ForceReset();
          }
      }
    }

    osDelay(50);
  }
  /* USER CODE END vTask_Bluetooth */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

