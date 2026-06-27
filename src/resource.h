#pragma once

// Icon resource ID
#define IDI_APP_ICON        101

// Tray icon IDs
#define IDM_TRAY_MENU       1001
#define IDM_TRAY_OPTIMIZE   1002
#define IDM_TRAY_SHOW       1003
#define IDM_TRAY_HIDE       1004
#define IDM_TRAY_EXIT       1005

// Opacity dialog
#define IDM_OPACITY_DIALOG  1100

// Display mode
#define IDM_DISPLAY_PERCENT 1200
#define IDM_DISPLAY_USED    1201
#define IDM_DISPLAY_AVAIL   1202

// Auto-start
#define IDM_AUTOSTART_TOGGLE 1250

// Timer IDs
#define IDT_MEMORY_REFRESH  2001
#define IDT_ANIMATION       2002
#define IDT_SELF_TRIM       2003

// Opacity limits (percent)
#define OPACITY_MIN         15
#define OPACITY_MAX         100

// Window dimensions
#define FLOAT_WINDOW_SIZE   64
#define FONT_NAME           L"Microsoft YaHei UI"

// Display mode enum
enum DisplayMode {
    DISPLAY_PERCENT = 0,
    DISPLAY_USED    = 1,
    DISPLAY_AVAIL   = 2
};
