#include "menu.h"
#include "smartwatch_ui.h"
#include "oled.h"
#include <string.h>

/* ==================== Menu Page Definitions ==================== */

const MenuPageDef_t menu_pages[5] = {
    [0] = { .title = "TIME DISPLAY", .item_count = 3, .items = {
        { "Current Time",  0,0,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Date",          0,1,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Step Count",    0,2,DETAIL_TYPE_INFO,     0,0,0,0 },
    }},
    [1] = { .title = "SENSOR DATA", .item_count = 3, .items = {
        { "Attitude",      1,0,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Acceleration",  1,1,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Temperature",   1,2,DETAIL_TYPE_INFO,     0,0,0,0 },
    }},
    [2] = { .title = "SPORTS DATA", .item_count = 3, .items = {
        { "Today's Steps", 2,0,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Distance",      2,1,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Calories",      2,2,DETAIL_TYPE_INFO,     0,0,0,0 },
    }},
    [3] = { .title = "SETTINGS", .item_count = 4, .items = {
        { "Time Set",      3,0,DETAIL_TYPE_TIME_SET, 0,0,23,1 },
        { "BT Pairing",    3,1,DETAIL_TYPE_TOGGLE,   0,0,1,1 },
        { "Brightness",    3,2,DETAIL_TYPE_NUMBER, 207,0,255,15 },
        { "Factory Reset", 3,3,DETAIL_TYPE_CONFIRM,  0,0,1,1 },
    }},
    [4] = { .title = "SYSTEM INFO", .item_count = 3, .items = {
        { "Battery",       4,0,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "Firmware",      4,1,DETAIL_TYPE_INFO,     0,0,0,0 },
        { "BT MAC",        4,2,DETAIL_TYPE_INFO,     0,0,0,0 },
    }},
};

/* ==================== Global Menu State ==================== */

MenuState_t g_menu_state;

/* ==================== Menu Initialization ==================== */

void Menu_Init(void) {
    g_menu_state.level       = MENU_LEVEL_MAIN;
    g_menu_state.main_idx    = 0;
    g_menu_state.sub_idx     = -1;
    g_menu_state.edit_value  = 0;
    g_menu_state.edit_min    = 0;
    g_menu_state.edit_max    = 0;
    g_menu_state.edit_step   = 0;
    g_menu_state.edit_field  = 0;
    g_menu_state.detail_type = DETAIL_TYPE_INFO;
    g_menu_state.btn_press_tick = 0;
}

/* ==================== Menu State Machine ==================== */

/* External reference to device data (defined in main.c) */
extern SmartWatchData_t watch_data;

/* Icon menu / watch cursor state (defined in smartwatch_ui.c) */
extern uint8_t g_watch_cursor;
extern uint8_t g_icon_menu_active;

/* External function from smartwatch_ui.c */
void IconMenu_SetSelection(int8_t target);

void Menu_HandleOp(MenuOp_t op) {
    uint8_t sc;

    /* ── Icon menu mode ── */
    if (g_icon_menu_active) {
        switch (op) {
            case MENU_OP_ROTATE_CW:  IconMenu_SetSelection(icon_sel + 1); break;
            case MENU_OP_ROTATE_CCW: IconMenu_SetSelection(icon_sel - 1); break;
            case MENU_OP_BTN_SHORT:
                switch (icon_sel) {
                    case 0:  /* 返回 → exit to watch face */
                        g_icon_menu_active = 0;
                        g_menu_state.main_idx = PAGE_WATCH_FACE;
                        g_menu_state.level = MENU_LEVEL_MAIN;
                        break;
                    case 1:  /* PEDO → launch pedometer app */
                        g_icon_menu_active = 0;
                        g_app_page = APP_PEDOMETER;
                        OLED_Clear();          /* 一次性清屏，进入计步器前消除残留 */
                        break;
                    case 2:  /* MPU → launch sensor app */
                        g_icon_menu_active = 0;
                        g_app_page = APP_SENSOR;
                        break;
                    default: /* indices 3-6: placeholder, no action */
                        break;
                }
                break;
            case MENU_OP_BTN_LONG:
                g_icon_menu_active = 0;  /* long press always exits to watch face */
                g_menu_state.main_idx = PAGE_WATCH_FACE;
                g_menu_state.level = MENU_LEVEL_MAIN;
                break;
            default: break;
        }
        return;
    }

    /* ── App page mode (full-screen overlay) ── */
    if (g_app_page != APP_NONE) {
        switch (op) {
            case MENU_OP_BTN_SHORT:
                if (g_app_page == APP_PEDOMETER) {
                    watch_data.step_count = 0;
                    MPU6050_Pedometer_Reset();   /* 同时复位 MPU6050 内部计步器 */
                }
                break;
            case MENU_OP_BTN_LONG:
                /* Long press: return to icon menu, auto-resume pedometer */
                g_app_page = APP_NONE;
                watch_data.pedo_paused = 0;
                g_icon_menu_active = 1;
                icon_x = 48;
                icon_animating = 0;
                break;
            default:
                break;
        }
        return;
    }

    switch (op) {
        case MENU_OP_ROTATE_CW:
            /* On watch face: move cursor between menu/settings */
            if (g_menu_state.level == MENU_LEVEL_MAIN && g_menu_state.main_idx == PAGE_WATCH_FACE) {
                g_watch_cursor = (g_watch_cursor + 1) % 2;
            } else if (g_menu_state.level == MENU_LEVEL_MAIN)
                g_menu_state.main_idx = (g_menu_state.main_idx + 1) % 5;
            else if (g_menu_state.level == MENU_LEVEL_SUB) {
                sc = menu_pages[g_menu_state.main_idx].item_count;
                if (g_menu_state.sub_idx < (int8_t)(sc - 1)) g_menu_state.sub_idx++;
            } else if (g_menu_state.level == MENU_LEVEL_DETAIL) {
                if (g_menu_state.detail_type != DETAIL_TYPE_INFO) {
                    g_menu_state.edit_value += g_menu_state.edit_step;
                    if (g_menu_state.edit_value > g_menu_state.edit_max)
                        g_menu_state.edit_value = g_menu_state.edit_max;
                }
            } break;
        case MENU_OP_ROTATE_CCW:
            if (g_menu_state.level == MENU_LEVEL_MAIN && g_menu_state.main_idx == PAGE_WATCH_FACE) {
                g_watch_cursor = (g_watch_cursor == 0) ? 1 : 0;
            } else if (g_menu_state.level == MENU_LEVEL_MAIN) {
                if (g_menu_state.main_idx == 0) g_menu_state.main_idx = 4;
                else g_menu_state.main_idx--;
            } else if (g_menu_state.level == MENU_LEVEL_SUB) {
                if (g_menu_state.sub_idx > 0) g_menu_state.sub_idx--;
            } else if (g_menu_state.level == MENU_LEVEL_DETAIL) {
                if (g_menu_state.detail_type != DETAIL_TYPE_INFO) {
                    g_menu_state.edit_value -= g_menu_state.edit_step;
                    if (g_menu_state.edit_value < g_menu_state.edit_min)
                        g_menu_state.edit_value = g_menu_state.edit_min;
                }
            } break;
        case MENU_OP_BTN_SHORT:
            if (g_menu_state.level == MENU_LEVEL_MAIN) {
                /* Watch face special: cursor selects menu or settings */
                if (g_menu_state.main_idx == PAGE_WATCH_FACE) {
                    if (g_watch_cursor == 0) {
                        /* 菜单: enter icon menu */
                        g_icon_menu_active = 1;
                        icon_sel = 0;
                        icon_x = 48;
                        icon_animating = 0;
                    } else {
                        /* 设置: go to settings sub-menu */
                        sc = menu_pages[PAGE_SETTINGS].item_count;
                        g_menu_state.level = MENU_LEVEL_SUB;
                        g_menu_state.main_idx = PAGE_SETTINGS;
                        g_menu_state.sub_idx = 0;
                    }
                } else {
                    sc = menu_pages[g_menu_state.main_idx].item_count;
                    if (sc > 0) { g_menu_state.level = MENU_LEVEL_SUB; g_menu_state.sub_idx = 0; }
                }
            } else if (g_menu_state.level == MENU_LEVEL_SUB) {
                const MenuItemDef_t *it = &menu_pages[g_menu_state.main_idx].items[g_menu_state.sub_idx];
                g_menu_state.level = MENU_LEVEL_DETAIL;
                g_menu_state.detail_type = it->detail_type;
                g_menu_state.edit_min = it->val_min;
                g_menu_state.edit_max = it->val_max;
                g_menu_state.edit_step = it->val_step;
                g_menu_state.edit_field = 0;
                switch (it->detail_type) {
                    case DETAIL_TYPE_INFO: g_menu_state.edit_value = 0; break;
                    case DETAIL_TYPE_TIME_SET:
                        g_menu_state.edit_value = watch_data.year;
                        g_menu_state.edit_min = 2024; g_menu_state.edit_max = 2099;
                        g_menu_state.edit_step = 1;
                        OLED_Clear();          /* 一次性清屏，进入时间设置前消除残留 */
                        break;
                    case DETAIL_TYPE_TOGGLE:
                        g_menu_state.edit_value = watch_data.bt_connected ? 1 : 0; break;
                    case DETAIL_TYPE_NUMBER:
                        g_menu_state.edit_value = (int32_t)watch_data.brightness; break;
                    case DETAIL_TYPE_CONFIRM:
                        g_menu_state.edit_value = 0; break;
                    default: g_menu_state.edit_value = it->def_val; break;
                }
            } else if (g_menu_state.level == MENU_LEVEL_DETAIL) {
                switch (g_menu_state.detail_type) {
                    case DETAIL_TYPE_INFO: g_menu_state.level = MENU_LEVEL_SUB; break;
                    case DETAIL_TYPE_TIME_SET:
                        if (g_menu_state.edit_field == 0) {
                            /* Commit year → month */
                            watch_data.year = (uint16_t)g_menu_state.edit_value;
                            g_menu_state.edit_field = 1;
                            g_menu_state.edit_value = watch_data.month;
                            g_menu_state.edit_min = 1; g_menu_state.edit_max = 12;
                            g_menu_state.edit_step = 1;
                        } else if (g_menu_state.edit_field == 1) {
                            /* Commit month → day */
                            watch_data.month = (uint8_t)g_menu_state.edit_value;
                            g_menu_state.edit_field = 2;
                            g_menu_state.edit_value = watch_data.day;
                            g_menu_state.edit_min = 1; g_menu_state.edit_max = 31;
                            g_menu_state.edit_step = 1;
                        } else if (g_menu_state.edit_field == 2) {
                            /* Commit day → hour */
                            watch_data.day = (uint8_t)g_menu_state.edit_value;
                            g_menu_state.edit_field = 3;
                            g_menu_state.edit_value = (int32_t)(watch_data.hour*100 + watch_data.minute);
                            g_menu_state.edit_min = 0; g_menu_state.edit_max = 2359;
                            g_menu_state.edit_step = 100;
                        } else if (g_menu_state.edit_field == 3) {
                            /* Switch to minute */
                            int32_t hp = g_menu_state.edit_value / 100;
                            g_menu_state.edit_field = 4;
                            g_menu_state.edit_min = hp * 100;
                            g_menu_state.edit_max = hp * 100 + 59;
                            g_menu_state.edit_step = 1;
                        } else {
                            /* Commit time, return to L1 */
                            int32_t v = g_menu_state.edit_value;
                            watch_data.hour = (uint8_t)(v/100);
                            watch_data.minute = (uint8_t)(v%100);
                            watch_data.second = 0;
                            watch_data.weekday = UI_CalcWeekday(watch_data.year, watch_data.month, watch_data.day);
                            UI_SaveSettings(&watch_data);
                            g_menu_state.level = MENU_LEVEL_SUB;
                        } break;
                    case DETAIL_TYPE_NUMBER:
                        watch_data.brightness = (uint8_t)g_menu_state.edit_value;
                        UI_SaveSettings(&watch_data);
                        g_menu_state.level = MENU_LEVEL_SUB; break;
                    case DETAIL_TYPE_TOGGLE:
                        watch_data.bt_connected = (g_menu_state.edit_value != 0) ? 1 : 0;
                        g_menu_state.level = MENU_LEVEL_SUB; break;
                    case DETAIL_TYPE_CONFIRM:
                        if (g_menu_state.edit_value != 0) UI_InitData(&watch_data);
                        g_menu_state.level = MENU_LEVEL_SUB; break;
                    default: g_menu_state.level = MENU_LEVEL_SUB; break;
                }
            } break;
        case MENU_OP_BTN_LONG:
            /* Long press on watch face: back from anywhere to watch face */
            if (g_menu_state.level == MENU_LEVEL_MAIN) {
                /* Already at main, toggle to settings quick-access */
                g_menu_state.level = MENU_LEVEL_SUB;
                g_menu_state.main_idx = PAGE_SETTINGS;
                g_menu_state.sub_idx = 0;
            } else if (g_menu_state.level == MENU_LEVEL_SUB) {
                g_menu_state.level = MENU_LEVEL_MAIN;
                g_menu_state.main_idx = PAGE_WATCH_FACE;
                g_menu_state.sub_idx = -1;
            } else if (g_menu_state.level == MENU_LEVEL_DETAIL) {
                g_menu_state.level = MENU_LEVEL_SUB;
            } break;
        default: break;
    }
}
