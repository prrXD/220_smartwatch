#ifndef __MENU_H
#define __MENU_H

#include "main.h"
#include <stdint.h>

/* ==================== Menu Levels ==================== */
typedef enum {
    MENU_LEVEL_MAIN   = 0,   /* L0: 5 main pages, encoder cycles pages, no cursor */
    MENU_LEVEL_SUB    = 1,   /* L1: sub-menu item list with ">" cursor */
    MENU_LEVEL_DETAIL = 2    /* L2: detail view or setting adjustment */
} MenuLevel_t;

/* ==================== Menu Operation Codes ==================== */
typedef enum {
    MENU_OP_NONE      = 0,
    MENU_OP_ROTATE_CW,       /* encoder turned clockwise */
    MENU_OP_ROTATE_CCW,      /* encoder turned counter-clockwise */
    MENU_OP_BTN_SHORT,       /* button short press (50-500ms) */
    MENU_OP_BTN_LONG         /* button long press (>1000ms) */
} MenuOp_t;

/* ==================== App Page Types (full-screen overlays) ==================== */
typedef enum {
    APP_NONE      = 0,   /* Not in an app page */
    APP_PEDOMETER = 1,   /* Pedometer step display */
    APP_SENSOR    = 2,   /* MPU6050 sensor data display */
    APP_VIDEO     = 3,   /* Video playback */
} AppPage_t;

/* ==================== Detail View Behavior Types ==================== */
typedef enum {
    DETAIL_TYPE_INFO    = 0, /* read-only information display */
    DETAIL_TYPE_TIME_SET,    /* time adjustment (hour/minute) */
    DETAIL_TYPE_NUMBER,      /* numeric value adjustment (brightness) */
    DETAIL_TYPE_CONFIRM,     /* confirm/cancel action (factory reset) */
    DETAIL_TYPE_TOGGLE       /* on/off toggle (BT pairing) */
} DetailType_t;

/* ==================== Menu Item Descriptor ==================== */
typedef struct {
    const char  *label;          /* display name (e.g., "Current Time") */
    uint8_t      parent_page;    /* which main page (0-4) */
    uint8_t      item_idx;       /* index within parent page's sub-items */
    DetailType_t detail_type;    /* L2 view behavior */
    int32_t      def_val;        /* default value when entering L2 */
    int32_t      val_min;        /* minimum allowed value */
    int32_t      val_max;        /* maximum allowed value */
    uint8_t      val_step;       /* step increment per encoder detent */
} MenuItemDef_t;

/* ==================== Menu Page Descriptor ==================== */
#define MENU_MAX_SUB_ITEMS  4

typedef struct {
    const char   *title;                           /* title shown in L1 list header */
    uint8_t       item_count;                      /* actual number of sub-items (1-4) */
    MenuItemDef_t items[MENU_MAX_SUB_ITEMS];       /* sub-item array */
} MenuPageDef_t;

/* ==================== Menu State (shared global) ==================== */
typedef struct {
    MenuLevel_t   level;            /* current menu level (L0/L1/L2) */
    uint8_t       main_idx;         /* 0-4: which main page is active */
    int8_t        sub_idx;          /* 0..N-1: selected sub-item in L1, current sub in L2 */
    int32_t       edit_value;       /* L2: live value being edited */
    int32_t       edit_min;         /* L2: minimum bound */
    int32_t       edit_max;         /* L2: maximum bound */
    uint8_t       edit_step;        /* L2: increment per encoder detent */
    uint8_t       edit_field;       /* sub-field index within L2 (0=hour, 1=minute for time set) */
    DetailType_t  detail_type;      /* L2: current detail view type */
    uint32_t      btn_press_tick;   /* OS tick when button was pressed down (0 = released) */
} MenuState_t;

/* ==================== Queue Message Payload ==================== */
typedef struct {
    MenuOp_t op;       /* operation code */
    int8_t   param;    /* extra parameter */
    uint8_t  reserved[2];
} MenuMsg_t;

/* ==================== Globals & API ==================== */

extern MenuState_t g_menu_state;
extern AppPage_t   g_app_page;       /* current full-screen app, APP_NONE if not in app */
extern const MenuPageDef_t menu_pages[];
#define MENU_PAGE_COUNT  5

void Menu_Init(void);
void Menu_HandleOp(MenuOp_t op);

#endif /* __MENU_H */
