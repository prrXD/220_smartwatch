#ifndef __SMARTWATCH_UI_H
#define __SMARTWATCH_UI_H

#include "main.h"
#include "mpu6050.h"
#include "menu.h"
#include <stdint.h>

/* ==================== Legacy Page Enum (kept for L0 rendering) ==================== */
typedef enum {
    PAGE_WATCH_FACE = 0,   /* Clock + date + step count */
    PAGE_IMU,              /* MPU6050 sensor detail (attitude, accel, temp) */
    PAGE_SPORTS,           /* Sports data (steps, distance, calories) */
    PAGE_SETTINGS,         /* Settings menu (time, BT, brightness, reset) */
    PAGE_SYSINFO,          /* System info (battery, FW, BT MAC) */
    PAGE_MAX
} UIPage_t;

/* ==================== Device Data (real hardware: MPU6050 + HC-05 Bluetooth) ==================== */
typedef struct {
    /* Time & date */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  weekday;       /* 0=Sun, 1=Mon ... 6=Sat */

    /* System info */
    uint8_t  battery_pct;
    int8_t   temp_celsius;  /* from MPU6050 temperature sensor */

    /* MPU6050 sensor data (I2C1, PB6/PB7, shared with OLED) */
    uint8_t  imu_status;        /* 1 = detected and working, 0 = not found */
    MPU6050_Accel_t accel;      /* acceleration in m/s^2 */
    MPU6050_Gyro_t  gyro;       /* angular velocity in deg/s */
    MPU6050_Angle_t angle;      /* pitch and roll in degrees */

    /* HC-05 Bluetooth status (USART2, PA2/PA3) */
    uint8_t  bt_connected;      /* 1 = phone connected (STATE pin high), 0 = disconnected */

    /* Pedometer */
    uint32_t step_count;        /* accumulated step count */
    uint8_t  pedo_paused;       /* 1 = counting paused, 0 = running */

    /* Settings */
    uint8_t  brightness;        /* screen brightness 0-255 */
} SmartWatchData_t;

/* ==================== UI Framework API ==================== */

/* Initialize device data with default values (time, sensor fields) */
void UI_InitData(SmartWatchData_t *data);

/* Main menu-aware renderer: dispatches L0/L1/L2 based on g_menu_state.level */
void UI_DrawMenu(SmartWatchData_t *data);

/* Legacy: Draw a single page at L0 (used for initial boot screen) */
void UI_DrawPage(UIPage_t page, SmartWatchData_t *data);

/* Draw status bar (top 8px: battery percentage, time, BT status dot, IMU status dot) */
void UI_DrawStatusBar(SmartWatchData_t *data);

/* Draw page indicator dots at bottom of screen (L0 only) */
void UI_DrawPageIndicator(uint8_t current, uint8_t total);

/* Icon menu state (extern for menu.c) */
extern uint8_t g_icon_menu_active;
extern uint8_t g_watch_cursor;
extern int8_t  icon_sel;
extern int8_t  icon_target;
extern int16_t icon_x;
extern uint8_t icon_animating;
void IconMenu_SetSelection(int8_t target);

/* App page state (full-screen overlay pages) */
extern AppPage_t g_app_page;       /* current full-screen app page */

/* Flash settings persistence */
void UI_SaveSettings(SmartWatchData_t *data);
void UI_LoadSettings(SmartWatchData_t *data);
uint8_t UI_CalcWeekday(uint16_t year, uint8_t month, uint8_t day);

#endif /* __SMARTWATCH_UI_H */
