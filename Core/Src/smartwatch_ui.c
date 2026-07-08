#include "smartwatch_ui.h"
#include "oled.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal_flash.h"

extern SmartWatchData_t watch_data;

/* Reference menu data (defined in menu.c) */
extern MenuState_t g_menu_state;
extern const MenuPageDef_t menu_pages[];

#define SSD1306_WIDTH  128
#define SSD1306_PAGES  8

/* Weekday strings */
static const char *weekdays_en[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

/* Formatting helpers */
static void format_signed_fixed1(char *buf, size_t size, float value) {
    int32_t scaled = (int32_t)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    char sign = '+';
    if (scaled < 0) { sign = '-'; scaled = -scaled; }
    snprintf(buf, size, "%c%ld.%ld", sign, (long)(scaled / 10), (long)(scaled % 10));
}
static void format_signed_int(char *buf, size_t size, float value) {
    int32_t rounded = (int32_t)(value + (value >= 0.0f ? 0.5f : -0.5f));
    char sign = '+';
    if (rounded < 0) { sign = '-'; rounded = -rounded; }
    snprintf(buf, size, "%c%ld", sign, (long)rounded);
}

void UI_InitData(SmartWatchData_t *data) {
    data->hour = 10; data->minute = 30; data->second = 45;
    data->year = 2026; data->month = 7; data->day = 9; data->weekday = 4;
    data->battery_pct = 85; data->temp_celsius = 25;
    data->imu_status = 0; data->bt_connected = 0; data->step_count = 0; data->pedo_paused = 0;
    data->brightness = 207;
    data->accel.ax = 0.0f; data->accel.ay = 0.0f; data->accel.az = 9.81f;
    data->gyro.gx = 0.0f; data->gyro.gy = 0.0f; data->gyro.gz = 0.0f;
    data->angle.pitch = 0.0f; data->angle.roll = 0.0f;
}

/* ==================== Flash Settings Persistence ==================== */

#define FLASH_SAVE_ADDR    ((uint32_t)0x0800FC00)
#define FLASH_SAVE_MAGIC   0xA5A5A5A5

typedef struct {
    uint32_t magic;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  weekday;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  brightness;
    uint8_t  _pad[15];
    uint32_t checksum;
} FlashSaveData_t;

void UI_SaveSettings(SmartWatchData_t *data) {
    /* Debug builds: skip flash saves so the debugger never times out.
       Define SKIP_FLASH_SAVE in your build flags or uncomment below. */
    // #define SKIP_FLASH_SAVE
#ifdef SKIP_FLASH_SAVE
    (void)data; return;
#else
    FlashSaveData_t save;
    save.magic     = FLASH_SAVE_MAGIC;
    save.year      = data->year;
    save.month     = data->month;
    save.day       = data->day;
    save.weekday   = data->weekday;
    save.hour      = data->hour;
    save.minute    = data->minute;
    save.second    = data->second;
    save.brightness = data->brightness;
    /* zero padding */
    memset(save._pad, 0, sizeof(save._pad));
    /* checksum: XOR all bytes before checksum field */
    uint32_t cs = 0;
    uint8_t *p = (uint8_t *)&save;
    for (uint32_t i = 0; i < sizeof(FlashSaveData_t) - sizeof(uint32_t); i++)
        cs ^= p[i];
    save.checksum = cs;

    /* Write to flash (last page, must be erased first) */
    /* Note: no __disable_irq() — HAL handles atomicity internally.
       Flash erase stalls the CPU ~20ms; interrupts stay enabled so the
       debugger's SWD connection survives. */
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = FLASH_SAVE_ADDR,
        .NbPages = 1
    };
    uint32_t page_error;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return;  /* erase failed — skip save */
    }
    uint32_t *src = (uint32_t *)&save;
    for (uint32_t i = 0; i < sizeof(FlashSaveData_t) / 4; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_SAVE_ADDR + i * 4, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return;  /* program failed — skip remaining */
        }
    }
    HAL_FLASH_Lock();
#endif /* SKIP_FLASH_SAVE */
}

void UI_LoadSettings(SmartWatchData_t *data) {
    FlashSaveData_t *saved = (FlashSaveData_t *)FLASH_SAVE_ADDR;
    if (saved->magic != FLASH_SAVE_MAGIC) return;
    /* verify checksum */
    uint32_t cs = 0;
    uint8_t *p = (uint8_t *)saved;
    for (uint32_t i = 0; i < sizeof(FlashSaveData_t) - sizeof(uint32_t); i++)
        cs ^= p[i];
    if (cs != saved->checksum) return;
    /* restore */
    data->year      = saved->year;
    data->month     = saved->month;
    data->day       = saved->day;
    data->weekday   = saved->weekday;
    data->hour      = saved->hour;
    data->minute    = saved->minute;
    data->second    = saved->second;
    data->brightness = saved->brightness;
}

uint8_t UI_CalcWeekday(uint16_t year, uint8_t month, uint8_t day) {
    /* Tomohiko Sakamoto's algorithm: returns 0=Sun ... 6=Sat */
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t y = year;
    if (month < 3) y -= 1;
    return (uint8_t)((y + y/4 - y/100 + y/400 + t[month-1] + day) % 7);
}

/* ==================== Status Bar ==================== */
void UI_DrawStatusBar(SmartWatchData_t *data) {
    uint16_t i; uint8_t x;
    for (i = 0; i < SSD1306_WIDTH; i++) OLED_Buffer[i] = 0x00;
    for (x = 0; x < 14; x++) {
        if (x == 0 || x == 13) OLED_Buffer[x] = 0x7E;
        else if (x == 14) OLED_Buffer[x] = 0x18;
        else OLED_Buffer[x] = 0x42;
    }
    uint8_t fill = (data->battery_pct * 10) / 100;
    if (fill > 10) fill = 10;
    for (x = 2; x < 2 + fill; x++) OLED_Buffer[x] = 0x5A;
    char bat[5]; snprintf(bat, sizeof(bat), "%d%%", data->battery_pct);
    OLED_DrawString6x8(18, 0, bat);
    char ts[9]; snprintf(ts, sizeof(ts), "%02d:%02d", data->hour, data->minute);
    OLED_DrawString6x8(60, 0, ts);
    if (data->bt_connected) OLED_DrawString6x8(102, 0, "BT");
    if (data->imu_status) OLED_DrawFilledCircle(123, 3, 2);
    else OLED_DrawCircle(123, 3, 2);
    for (x = 0; x < SSD1306_WIDTH; x++) OLED_SetPixel(x, 7, 1);
}

/* ==================== Helper ==================== */
static void draw_sep(void) {
    uint8_t x;
    for (x = 0; x < SSD1306_WIDTH; x++) OLED_SetPixel(x, 24, 1);
}

/* ==================== Page Indicator ==================== */
void UI_DrawPageIndicator(uint8_t current, uint8_t total) {
    uint16_t i; uint8_t x, j;
    for (i = 7 * SSD1306_WIDTH; i < SSD1306_WIDTH * SSD1306_PAGES; i++)
        OLED_Buffer[i] = 0x00;
    for (x = 0; x < SSD1306_WIDTH; x++) OLED_SetPixel(x, 56, 1);
    uint8_t sp = 14, tw = (total - 1) * sp, sx = (SSD1306_WIDTH - tw) / 2;
    for (j = 0; j < total; j++) {
        uint8_t cx = sx + j * sp;
        if (j == current) OLED_DrawFilledCircle(cx, 60, 2);
        else OLED_DrawCircle(cx, 60, 2);
    }
}

/* ==================== Level 0: Main Pages ==================== */

/* ==================== Chinese-style Watch Face ==================== */

uint8_t g_watch_cursor = 0;     /* 0=菜单, 1=设置 */
uint8_t g_icon_menu_active = 0;  /* 1 = icon menu is active, overrides normal UI */
AppPage_t g_app_page = APP_NONE;     /* current full-screen app page */

static void L0_WatchFace(SmartWatchData_t *data) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);

    /* Date row: YYYY-MM-DD Weekday */
    char ds[20];
    snprintf(ds, sizeof(ds), "%04d-%02d-%02d %s",
        data->year, data->month, data->day, weekdays_en[data->weekday]);
    OLED_DrawString6x8((128 - strlen(ds)*6)/2, 1, ds);

    /* Large time: HH:MM:SS */
    int h = data->hour, m = data->minute, s = data->second;
    if (h < 0) h = 0;
    if (h > 23) h = 23;
    if (m < 0) m = 0;
    if (m > 59) m = 59;
    if (s < 0) s = 0;
    if (s > 59) s = 59;
    char ts[9];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", h, m, s);
    OLED_DrawString8x16((128 - strlen(ts)*8)/2, 3, ts);

    /* Bottom: 菜单 | 设置  with cursor highlight */
    OLED_DrawCNChar16(0,  48, "\xb2\xcb");  /* 菜 */
    OLED_DrawCNChar16(16, 48, "\xb5\xa5");  /* 单 */
    OLED_DrawCNChar16(96, 48, "\xc9\xe8");  /* 设 */
    OLED_DrawCNChar16(112,48, "\xd6\xc3");  /* 置 */

    /* Highlight selected option */
    uint8_t cx = (g_watch_cursor == 0) ? 0 : 92;
    OLED_ReverseArea(cx, 48, 36, 16);
}

static void L0_IMU(SmartWatchData_t *data) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);
    OLED_DrawString8x16(4, 1, "SENSORS"); draw_sep();
    if (!data->imu_status) {
        OLED_DrawString6x8(4, 4, "MPU6050: NOT FOUND");
        OLED_DrawString6x8(4, 5, "PB6/PB7  I2C1");
        OLED_DrawString6x8(4, 6, "Auto-retry every 3s");
    } else {
        char buf[40], ax[8], ay[8], az[8], pitch[8], roll[8];
        format_signed_fixed1(ax, sizeof(ax), data->accel.ax);
        format_signed_fixed1(ay, sizeof(ay), data->accel.ay);
        format_signed_fixed1(az, sizeof(az), data->accel.az);
        format_signed_int(pitch, sizeof(pitch), data->angle.pitch);
        format_signed_int(roll, sizeof(roll), data->angle.roll);
        snprintf(buf, sizeof(buf), "Pitch:%s Roll:%s deg", pitch, roll);
        OLED_DrawString6x8(0, 3, buf);
        snprintf(buf, sizeof(buf), "ax:%s ay:%s az:%s", ax, ay, az);
        OLED_DrawString6x8(0, 4, buf);
        snprintf(buf, sizeof(buf), "Temp: %dC  %s", data->temp_celsius,
                 g_mpu6050_i2c_error ? "ERR" : "OK");
        OLED_DrawString6x8(0, 6, buf);
    }
    UI_DrawPageIndicator(PAGE_IMU, 5);
}

static void L0_Sports(SmartWatchData_t *data) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);
    OLED_DrawString8x16(4, 1, "SPORTS"); draw_sep();
    char ss[16]; snprintf(ss, sizeof(ss), "%lu", (unsigned long)data->step_count);
    OLED_DrawString8x16((128 - strlen(ss)*8)/2, 3, ss);
    OLED_DrawString6x8(4, 3, "Steps:");
    char ds[16]; snprintf(ds, sizeof(ds), "Dist: %u m", (uint16_t)(data->step_count*0.7f));
    OLED_DrawString6x8(4, 6, ds);
    UI_DrawPageIndicator(PAGE_SPORTS, 5);
}

static void L0_Settings(SmartWatchData_t *data, int8_t cur) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);
    OLED_DrawString8x16(4, 1, "SETTINGS"); draw_sep();
    if (cur >= 0 && cur <= 3)
        OLED_DrawString6x8(2, 3 + (uint8_t)cur, ">");
    OLED_DrawString6x8(20, 3, "Time Set");
    OLED_DrawString6x8(20, 4, "BT Pairing");
    OLED_DrawString6x8(20, 5, "Brightness");
    OLED_DrawString6x8(20, 6, "Factory Reset");
    if (cur < 0)
        OLED_DrawString6x8(4, 7, "Rotate: move  Short: select");
    else
        OLED_DrawString6x8(4, 7, "Short: confirm  Long: back");
    UI_DrawPageIndicator(PAGE_SETTINGS, 5);
}

static void L0_SysInfo(SmartWatchData_t *data) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);
    OLED_DrawString8x16(4, 1, "SYS INFO"); draw_sep();
    char bs[16]; snprintf(bs, sizeof(bs), "Battery: %d%%", data->battery_pct);
    OLED_DrawString6x8(4, 3, bs);
    OLED_DrawString6x8(4, 4, "FW: SmartWatch v1.0");
    OLED_DrawString6x8(4, 5, "BT: HC-05 USART2");
    UI_DrawPageIndicator(PAGE_SYSINFO, 5);
}

static void UI_DrawMainPage(uint8_t idx, SmartWatchData_t *data) {
    switch (idx) {
        case PAGE_WATCH_FACE: L0_WatchFace(data);        break;
        case PAGE_IMU:        L0_IMU(data);              break;
        case PAGE_SPORTS:     L0_Sports(data);           break;
        case PAGE_SETTINGS:   L0_Settings(data, -1);     break;
        case PAGE_SYSINFO:    L0_SysInfo(data);          break;
        default: break;
    }
}

/* ==================== Icon Sliding Menu ==================== */

int8_t  icon_sel = 0;       /* currently selected icon index (0-6) */
int8_t  icon_target = 0;    /* target icon index */
int16_t icon_x = 48;        /* animation x-offset of center icon */
uint8_t icon_animating = 0; /* 1 = animation in progress */
#define ICON_SPEED 8               /* pixels per frame */

/* Render the icon sliding menu */
static void L0_IconMenu(SmartWatchData_t *data) {
    OLED_ClearBuffer(); UI_DrawStatusBar(data);

    /* Animate toward target */
    if (icon_animating) {
        int16_t target_x = 48;  /* center position */
        if (icon_x < target_x) { icon_x += ICON_SPEED; if (icon_x >= target_x) { icon_x = target_x; icon_animating = 0; } }
        if (icon_x > target_x) { icon_x -= ICON_SPEED; if (icon_x <= target_x) { icon_x = target_x; icon_animating = 0; } }
    }

    /* Draw icons: previous, current, next */
    int16_t cx = icon_x;
    if (icon_sel >= 1) OLED_DrawBitmap(cx - 48, 16, Menu_Graph[icon_sel - 1], 32, 32);
    OLED_DrawBitmap(cx,      16, Menu_Graph[icon_sel],     32, 32);
    if (icon_sel < 6) OLED_DrawBitmap(cx + 48, 16, Menu_Graph[icon_sel + 1], 32, 32);

    /* Draw Frame around center icon */
    OLED_DrawRect(cx - 6, 10, 44, 44);

    /* Icon labels (Chinese) */
    /* Icon labels -- Chinese for index 0, ASCII for indices 1-6 */
    static const char *icon_labels[] = {
        "\xb7\xb5\xbb\xd8",  /* [0] 返回 -- Chinese, keep existing */
        "PEDO",               /* [1] Pedometer -- ASCII */
        "MPU",                /* [2] MPU6050 Sensor -- ASCII */
        "---",                /* [3] Placeholder -- ASCII */
        "---",                /* [4] Placeholder -- ASCII */
        "---",                /* [5] Placeholder -- ASCII */
        "---",                /* [6] Placeholder -- ASCII */
    };
    /* ── Icon label: Chinese or ASCII ── */
    if (icon_sel == 0) {
        /* Chinese label (返回): use 16x16 Chinese font */
        OLED_DrawStringCN16(0, 50, icon_labels[0]);
    } else {
        /* ASCII label: use 8x16 font, centered */
        uint8_t lbl_len = strlen(icon_labels[icon_sel]);
        uint8_t lbl_w   = lbl_len * 8;
        uint8_t lbl_x   = (SSD1306_WIDTH - lbl_w) / 2;
        OLED_DrawString8x16(lbl_x, 6, icon_labels[icon_sel]);
    }
}

/* Start icon menu animation to new selection */
void IconMenu_SetSelection(int8_t target) {
    if (target < 0) target = 6;
    if (target > 6) target = 0;
    if (target != icon_sel) {
        icon_target = target;
        if (target > icon_sel) icon_x -= 48;  /* slide left */
        else                    icon_x += 48;  /* slide right */
        icon_sel = target;
        icon_animating = 1;
    }
}

/* ==================== App Pages (full-screen overlays) ==================== */

/* ── Pedometer App Page ── */
static void L0_PedometerApp(SmartWatchData_t *data) {
    OLED_ClearBuffer();       /* 只清缓存，不刷屏 → 无闪烁 */

    /* ── "steps 12345.0" in 8x16 font (same size as time display) ── */
    char buf[24];
    /* 嵌入式平台 snprintf 不支持 %f，用手动整数拆分实现小数显示 */
    snprintf(buf, sizeof(buf), "steps %lu", (unsigned long)data->step_count);
    OLED_DrawString8x16((SSD1306_WIDTH - strlen(buf) * 8) / 2, 2, buf);

    /* ── Stats below ── */
    uint32_t dist_m = data->step_count / 2;                    /* 每步 0.5 米 */
    uint32_t cal_whole = (data->step_count / 100) * 3
                       + ((data->step_count % 100) * 3) / 100; /* 每步 0.03 kcal */
    uint32_t cal_frac  = (((data->step_count % 100) * 3) % 100) / 10;

    if (dist_m >= 1000) {
        uint32_t d_km   = dist_m / 1000;
        uint32_t d_frac = (dist_m % 1000) / 10;
        snprintf(buf, sizeof(buf), "D:%lu.%02lukm", d_km, d_frac);
    } else {
        snprintf(buf, sizeof(buf), "D:%lum", dist_m);
    }
    OLED_DrawString6x8((SSD1306_WIDTH - strlen(buf) * 6) / 2, 5, buf);

    snprintf(buf, sizeof(buf), "C:%lu.%lukcal", cal_whole, cal_frac);
    OLED_DrawString6x8((SSD1306_WIDTH - strlen(buf) * 6) / 2, 6, buf);

    /* ── 右下角清零按钮提示 ── */
    OLED_DrawString6x8(110, 7, "CLR");

    /* ── 左下角暂停指示 ── */
    if (data->pedo_paused) {
        OLED_DrawString6x8(0, 7, "||");
    }
}

/* ── Sensor Data App Page ── */
static void L0_SensorApp(SmartWatchData_t *data) {
    OLED_ClearBuffer();
    UI_DrawStatusBar(data);

    /* Title */
    OLED_DrawString8x16(16, 1, "MPU6050 DATA");
    draw_sep();

    if (!data->imu_status) {
        OLED_DrawString6x8(4, 4, "MPU6050: NOT FOUND");
        OLED_DrawString6x8(4, 5, "Check I2C connection");
    } else {
        /* Attitude (pitch, roll) */
        char buf[32], pitch[8], roll[8], ax_s[8], ay_s[8], az_s[8];
        format_signed_int(pitch, sizeof(pitch), data->angle.pitch);
        format_signed_int(roll,  sizeof(roll),  data->angle.roll);
        snprintf(buf, sizeof(buf), "Pitch: %s  Roll: %s", pitch, roll);
        OLED_DrawString6x8(0, 4, buf);

        /* Acceleration */
        format_signed_fixed1(ax_s, sizeof(ax_s), data->accel.ax);
        format_signed_fixed1(ay_s, sizeof(ay_s), data->accel.ay);
        format_signed_fixed1(az_s, sizeof(az_s), data->accel.az);
        snprintf(buf, sizeof(buf), "ax:%s ay:%s", ax_s, ay_s);
        OLED_DrawString6x8(0, 5, buf);
        snprintf(buf, sizeof(buf), "az:%s", az_s);
        OLED_DrawString6x8(0, 6, buf);

        /* Temperature */
        snprintf(buf, sizeof(buf), "Temp: %d C", data->temp_celsius);
        OLED_DrawString6x8(0, 7, buf);
    }
}

/* ==================== Level 1: Sub-Menu ==================== */
static void UI_DrawSubMenuList(SmartWatchData_t *data) {
    const MenuPageDef_t *pg = &menu_pages[g_menu_state.main_idx];
    uint8_t n = pg->item_count, i;
    OLED_ClearBuffer(); UI_DrawStatusBar(data);
    OLED_DrawString8x16(4, 1, (char *)pg->title); draw_sep();
    for (i = 0; i < n; i++) {
        uint8_t yp = 3 + i;
        if ((int8_t)i == g_menu_state.sub_idx) {
            OLED_DrawString6x8(2, yp, ">");
            OLED_DrawString6x8(16, yp, (char *)pg->items[i].label);
        } else {
            OLED_DrawString6x8(10, yp, (char *)pg->items[i].label);
        }
    }
}

/* ==================== Level 2: Detail Views ==================== */
static void detail_hdr(const char *t, SmartWatchData_t *d) {
    OLED_ClearBuffer(); UI_DrawStatusBar(d);
    OLED_DrawString8x16(4, 1, (char *)t); draw_sep();
}
static void detail_hint(void) { /* intentionally blank — hints removed */ }

static void V_CurrentTime(SmartWatchData_t *d) {
    detail_hdr("CURRENT TIME", d); char b[12];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", d->hour, d->minute, d->second);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b); detail_hint();
}
static void V_Date(SmartWatchData_t *d) {
    detail_hdr("DATE", d); char b[32];
    snprintf(b, sizeof(b), "%04d-%02d-%02d", d->year, d->month, d->day);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b);
    snprintf(b, sizeof(b), "%s", weekdays_en[d->weekday]);
    OLED_DrawString6x8((128 - strlen(b)*6)/2, 6, b); detail_hint();
}
static void V_StepCount(SmartWatchData_t *d) {
    detail_hdr("STEP COUNT", d); char b[16];
    snprintf(b, sizeof(b), "%lu", (unsigned long)d->step_count);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b); detail_hint();
}
static void V_Attitude(SmartWatchData_t *d) {
    detail_hdr("ATTITUDE", d);
    if (!d->imu_status) { OLED_DrawString6x8(4, 4, "MPU6050: NOT FOUND"); }
    else { char b[32], p[8], r[8];
        format_signed_int(p, sizeof(p), d->angle.pitch);
        format_signed_int(r, sizeof(r), d->angle.roll);
        snprintf(b, sizeof(b), "Pitch: %s deg", p); OLED_DrawString8x16(4, 3, b);
        snprintf(b, sizeof(b), "Roll:  %s deg", r); OLED_DrawString8x16(4, 5, b);
    } detail_hint();
}
static void V_Accel(SmartWatchData_t *d) {
    detail_hdr("ACCELERATION", d);
    if (!d->imu_status) { OLED_DrawString6x8(4, 4, "MPU6050: NOT FOUND"); }
    else { char b[32], ax[8], ay[8], az[8];
        format_signed_fixed1(ax, sizeof(ax), d->accel.ax);
        format_signed_fixed1(ay, sizeof(ay), d->accel.ay);
        format_signed_fixed1(az, sizeof(az), d->accel.az);
        snprintf(b, sizeof(b), "ax: %s", ax); OLED_DrawString6x8(4, 3, b);
        snprintf(b, sizeof(b), "ay: %s", ay); OLED_DrawString6x8(4, 4, b);
        snprintf(b, sizeof(b), "az: %s", az); OLED_DrawString6x8(4, 5, b);
        OLED_DrawString6x8(4, 6, "m/s^2");
    } detail_hint();
}
static void V_Temp(SmartWatchData_t *d) {
    detail_hdr("TEMPERATURE", d);
    if (!d->imu_status) { OLED_DrawString6x8(4, 4, "MPU6050: NOT FOUND"); }
    else { char b[16]; snprintf(b, sizeof(b), "%d C", d->temp_celsius);
        OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b); }
    detail_hint();
}
static void V_TodaySteps(SmartWatchData_t *d) {
    detail_hdr("TODAY STEPS", d); char b[16];
    snprintf(b, sizeof(b), "%lu", (unsigned long)d->step_count);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b); detail_hint();
}
static void V_Distance(SmartWatchData_t *d) {
    detail_hdr("DISTANCE", d); char b[16]; uint16_t dm = (uint16_t)(d->step_count*0.7f);
    if (dm >= 1000) snprintf(b, sizeof(b), "%.2f km", dm/1000.0f);
    else snprintf(b, sizeof(b), "%u m", dm);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b);
    snprintf(b, sizeof(b), "(%lu steps)", (unsigned long)d->step_count);
    OLED_DrawString6x8(4, 5, b); detail_hint();
}
static void V_Calories(SmartWatchData_t *d) {
    detail_hdr("CALORIES", d); char b[16]; uint16_t kc = (uint16_t)(d->step_count*0.04f);
    snprintf(b, sizeof(b), "%u kcal", kc);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b);
    snprintf(b, sizeof(b), "(%lu steps)", (unsigned long)d->step_count);
    OLED_DrawString6x8(4, 5, b); detail_hint();
}
static void V_Battery(SmartWatchData_t *d) {
    detail_hdr("BATTERY", d); char b[16];
    snprintf(b, sizeof(b), "%d%%", d->battery_pct);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b);
    uint8_t bw = d->battery_pct, bx, pg;
    for (bx = 14; bx < 14 + bw && bx < 114; bx++)
        for (pg = 4; pg <= 6; pg++) { uint8_t y = pg*8;
            OLED_SetPixel(bx, y, 1); OLED_SetPixel(bx, y+2, 1);
            OLED_SetPixel(bx, y+4, 1); OLED_SetPixel(bx, y+6, 1); }
    for (bx = 13; bx < 115; bx++) { OLED_SetPixel(bx, 32, 1); OLED_SetPixel(bx, 55, 1); }
    for (pg = 4; pg <= 6; pg++) { uint8_t y = pg*8;
        OLED_SetPixel(13, y, 1); OLED_SetPixel(114, y, 1); }
    detail_hint();
}
static void V_Firmware(SmartWatchData_t *d) {
    (void)d; detail_hdr("FIRMWARE", d);
    OLED_DrawString8x16(4, 3, "SmartWatch");
    OLED_DrawString6x8(4, 5, "Version: v1.0");
    OLED_DrawString6x8(4, 6, "STM32F103C8T6"); detail_hint();
}
static void V_BTMAC(SmartWatchData_t *d) {
    (void)d; detail_hdr("BT MAC", d);
    OLED_DrawString6x8(4, 3, "Module: HC-05");
    OLED_DrawString6x8(4, 4, "Interface: USART2");
    OLED_DrawString6x8(4, 5, "Baud: 9600");
    OLED_DrawString6x8(4, 6, "Status: OK"); detail_hint();
}

/* Setting views */
static void S_TimeSet(SmartWatchData_t *data) {
    OLED_ClearBuffer();       /* 只清缓存，不刷屏 → 无闪烁 */

    char buf[24];

    /* ── Date: YYYY-MM-DD in 8x16 font (page 1, centered) ── */
    uint16_t y_disp = data->year;
    uint8_t  m_disp = data->month;
    uint8_t  d_disp = data->day;
    if (g_menu_state.edit_field == 0)
        y_disp = (uint16_t)g_menu_state.edit_value;
    else if (g_menu_state.edit_field == 1)
        m_disp = (uint8_t)g_menu_state.edit_value;
    else if (g_menu_state.edit_field == 2)
        d_disp = (uint8_t)g_menu_state.edit_value;
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y_disp, m_disp, d_disp);
    OLED_DrawString8x16(24, 1, buf);

    /* ── Time: HH:MM in 8x16 font (page 3, centered) ── */
    int32_t v = g_menu_state.edit_value;
    uint8_t hh = (uint8_t)(v / 100), mm = (uint8_t)(v % 100);
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    OLED_DrawString8x16(44, 3, buf);

    /* ── Underline active field ── */
    uint8_t ux_start = 0, ux_end = 0, uy = 0;
    switch (g_menu_state.edit_field) {
        case 0: /* Year */    ux_start = 24; ux_end = 47; uy = 23; break;
        case 1: /* Month */   ux_start = 64; ux_end = 79; uy = 23; break;
        case 2: /* Day */     ux_start = 88; ux_end = 103; uy = 23; break;
        case 3: /* Hour */    ux_start = 44; ux_end = 59; uy = 39; break;
        case 4: /* Minute */  ux_start = 68; ux_end = 83; uy = 39; break;
    }
    for (uint8_t ux = ux_start; ux < ux_end; ux++)
        OLED_SetPixel(ux, uy, 1);

    /* ── Field hint at bottom ── */
    static const char *field_names[] = {"Year", "Month", "Day", "Hour", "Minute"};
    snprintf(buf, sizeof(buf), "> %s", field_names[g_menu_state.edit_field]);
    OLED_DrawString6x8((128 - strlen(buf)*6)/2, 6, buf);
}
static void S_BTPairing(SmartWatchData_t *d) {
    detail_hdr("BT PAIRING", d);
    OLED_DrawString6x8(4, 3, "Status:");
    OLED_DrawString6x8(4, 4, d->bt_connected ? "  Connected" : "  Disconnected");
}
static void S_Brightness(SmartWatchData_t *d) {
    detail_hdr("BRIGHTNESS", d); int32_t bv = g_menu_state.edit_value; char b[16];
    snprintf(b, sizeof(b), "%ld", (long)bv);
    OLED_DrawString8x16((128 - strlen(b)*8)/2, 3, b);
    uint8_t bx, fill = (uint8_t)((bv * 100) / 255);
    for (bx = 14; bx < 14 + fill; bx++) {
        OLED_SetPixel(bx, 41, 1); OLED_SetPixel(bx, 42, 1); OLED_SetPixel(bx, 43, 1);
        OLED_SetPixel(bx, 44, 1); OLED_SetPixel(bx, 45, 1); OLED_SetPixel(bx, 46, 1);
    }
    for (bx = 13; bx < 115; bx++) { OLED_SetPixel(bx, 40, 1); OLED_SetPixel(bx, 47, 1); }
    /* 实时调节屏幕亮度（旋转编码器时立即生效） */
    OLED_SetContrast((uint8_t)bv);
}
static void S_FactoryReset(SmartWatchData_t *d) {
    (void)d; detail_hdr("FACTORY RESET", d);
    OLED_DrawString6x8(4, 3, "Reset all settings?");
    OLED_DrawString6x8(4, 4, "This cannot be undone.");
    if (g_menu_state.edit_value == 0) {
        OLED_DrawString6x8(2, 5, "> CANCEL"); OLED_DrawString6x8(20, 6, "  CONFIRM");
    } else {
        OLED_DrawString6x8(2, 5, "  CANCEL"); OLED_DrawString6x8(20, 6, "> CONFIRM");
    }
}

/* L2 dispatcher */
static void UI_DrawDetailView(SmartWatchData_t *data) {
    uint8_t mi = g_menu_state.main_idx;
    int8_t  si = g_menu_state.sub_idx;
    if (mi >= 5 || si < 0 || si >= (int8_t)menu_pages[mi].item_count) {
        g_menu_state.level = MENU_LEVEL_MAIN; UI_DrawMainPage(mi, data); return;
    }
    const MenuItemDef_t *it = &menu_pages[mi].items[si];
    switch (it->detail_type) {
        case DETAIL_TYPE_INFO:
            switch (mi) {
                case 0: switch (si) {
                    case 0: V_CurrentTime(data); break;
                    case 1: V_Date(data);        break;
                    case 2: V_StepCount(data);   break;
                } break;
                case 1: switch (si) {
                    case 0: V_Attitude(data); break;
                    case 1: V_Accel(data);    break;
                    case 2: V_Temp(data);     break;
                } break;
                case 2: switch (si) {
                    case 0: V_TodaySteps(data); break;
                    case 1: V_Distance(data);   break;
                    case 2: V_Calories(data);   break;
                } break;
                case 4: switch (si) {
                    case 0: V_Battery(data);   break;
                    case 1: V_Firmware(data);  break;
                    case 2: V_BTMAC(data);     break;
                } break;
            } break;
        case DETAIL_TYPE_TIME_SET: S_TimeSet(data);     break;
        case DETAIL_TYPE_TOGGLE:   S_BTPairing(data);   break;
        case DETAIL_TYPE_NUMBER:   S_Brightness(data);  break;
        case DETAIL_TYPE_CONFIRM:  S_FactoryReset(data); break;
        default: break;
    }
}

/* ==================== Top-Level Menu Dispatcher ==================== */
void UI_DrawMenu(SmartWatchData_t *data) {
    if (g_icon_menu_active) {
        L0_IconMenu(data);
        OLED_Update();
        return;
    }
    /* Check for active full-screen app page */
    if (g_app_page != APP_NONE) {
        switch (g_app_page) {
            case APP_PEDOMETER: L0_PedometerApp(data); break;
            case APP_SENSOR:    L0_SensorApp(data);    break;
            default:                                   break;
        }
        OLED_Update();
        return;
    }
    /* Normal L0/L1/L2 menu dispatch */
    switch (g_menu_state.level) {
        case MENU_LEVEL_MAIN:   UI_DrawMainPage(g_menu_state.main_idx, data); break;
        case MENU_LEVEL_SUB:    UI_DrawSubMenuList(data);                     break;
        case MENU_LEVEL_DETAIL: UI_DrawDetailView(data);                     break;
        default:                UI_DrawMainPage(PAGE_WATCH_FACE, data);       break;
    }
    OLED_Update();
}

/* Legacy */
void UI_DrawPage(UIPage_t page, SmartWatchData_t *data) {
    g_menu_state.level = MENU_LEVEL_MAIN;
    g_menu_state.main_idx = (uint8_t)page;
    g_menu_state.sub_idx = -1;
    UI_DrawMenu(data);
}

