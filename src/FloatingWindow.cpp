// FloatingWindow.cpp — Always-on-top memory display + system tray
//
// Memory optimizations for the app itself:
//   1. Self-trim after init and after each optimization cycle
//   2. GDI brushes/pens created once, shared across paints
//   3. Stack-allocated buffers (swprintf_s) — no heap churn
//   4. Config file is tiny, only read/written on change
//   5. Separate self-trim timer avoids coupling with display refresh

#include "FloatingWindow.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <shellapi.h>
#include <commctrl.h>
#include <windowsx.h>

const wchar_t* FloatingWindow::CLASS_NAME = L"MemoryOptimizerFloatingWnd";

// Config file (next to .exe)
#define CONFIG_FILE L"MemoryOptimizer.ini"

// Dialog control IDs
#define IDC_SLIDER      200
#define IDC_LABEL_PCT   201
#define IDC_LABEL_VAL   202

// ----------------------------------------------------------------------------
// Helper: get directory containing the .exe
// ----------------------------------------------------------------------------
static void GetExeDir(wchar_t* buf, size_t len) {
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(len));
    wchar_t* slash = wcsrchr(buf, L'\\');
    if (slash) *(slash + 1) = L'\0';
}

// Convert percent (15-100) → alpha (38-255)
static inline BYTE PercentToAlpha(int pct) {
    return static_cast<BYTE>((pct * 255) / 100);
}

// Convert alpha → percent
static inline int AlphaToPercent(BYTE alpha) {
    int pct = (static_cast<int>(alpha) * 100) / 255;
    if (pct < OPACITY_MIN) pct = OPACITY_MIN;
    if (pct > OPACITY_MAX) pct = OPACITY_MAX;
    return pct;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

FloatingWindow::FloatingWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance)
    , m_hWnd(nullptr), m_hTooltip(nullptr)
    , m_hFont(nullptr), m_hFontSmall(nullptr)
    , m_hBrushBg(nullptr), m_hBrushGreen(nullptr)
    , m_hBrushYellow(nullptr), m_hBrushRed(nullptr)
    , m_hPenBorder(nullptr), m_hPenGray(nullptr)
    , m_bVisible(true), m_bDragging(false)
    , m_dragStartX(0), m_dragStartY(0)
    , m_lastClickTime(0)
    , m_bAnimating(false), m_animFrame(0)
    , m_animColor(RGB(0,0,0))
    , m_opacityPercent(70)
    , m_displayMode(DISPLAY_PERCENT)
{
    m_memInfo = {};
    LoadConfig();
    CreateUIBrushes();
}

FloatingWindow::~FloatingWindow() {
    SaveConfig();
    RemoveTrayIcon();
    CleanupUIBrushes();
}

// ============================================================================
// Config Persistence
// ============================================================================

void FloatingWindow::LoadConfig() {
    wchar_t path[MAX_PATH];
    GetExeDir(path, MAX_PATH);
    wcscat_s(path, MAX_PATH, CONFIG_FILE);

    m_opacityPercent = 70;
    m_displayMode    = DISPLAY_PERCENT;

    FILE* f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) return;

    wchar_t line[128];
    while (fgetws(line, 128, f)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len-1] == L'\n' || line[len-1] == L'\r'))
            line[--len] = L'\0';

        if (wcsncmp(line, L"opacity=", 8) == 0) {
            int v = _wtoi(line + 8);
            // Read as percent; clamp to valid range
            if (v < OPACITY_MIN) v = OPACITY_MIN;
            if (v > OPACITY_MAX) v = OPACITY_MAX;
            m_opacityPercent = v;
        }
        else if (wcsncmp(line, L"display=", 8) == 0) {
            int v = _wtoi(line + 8);
            if (v >= 0 && v <= 2) m_displayMode = v;
        }
    }
    fclose(f);
}

void FloatingWindow::SaveConfig() {
    wchar_t path[MAX_PATH];
    GetExeDir(path, MAX_PATH);
    wcscat_s(path, MAX_PATH, CONFIG_FILE);

    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"opacity=%d\r\ndisplay=%d\r\n", m_opacityPercent, m_displayMode);
    fclose(f);
}

// ============================================================================
// GDI Resources
// ============================================================================

void FloatingWindow::CreateUIBrushes() {
    m_hFont = CreateFontW(
        22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, FONT_NAME);
    m_hFontSmall = CreateFontW(
        10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, FONT_NAME);
    m_hBrushBg    = CreateSolidBrush(RGB(28, 28, 30));
    m_hBrushGreen = CreateSolidBrush(RGB(72, 175, 76));
    m_hBrushYellow= CreateSolidBrush(RGB(250, 188, 5));
    m_hBrushRed   = CreateSolidBrush(RGB(235, 60, 50));
    m_hPenBorder  = CreatePen(PS_SOLID, 1, RGB(70, 70, 75));
    m_hPenGray    = CreatePen(PS_SOLID, 2, RGB(55, 55, 60));
}

void FloatingWindow::CleanupUIBrushes() {
    if (m_hFont)       DeleteObject(m_hFont);
    if (m_hFontSmall)  DeleteObject(m_hFontSmall);
    if (m_hBrushBg)    DeleteObject(m_hBrushBg);
    if (m_hBrushGreen) DeleteObject(m_hBrushGreen);
    if (m_hBrushYellow)DeleteObject(m_hBrushYellow);
    if (m_hBrushRed)   DeleteObject(m_hBrushRed);
    if (m_hPenBorder)  DeleteObject(m_hPenBorder);
    if (m_hPenGray)    DeleteObject(m_hPenGray);
}

HBRUSH FloatingWindow::GetColorBrush(int percent) {
    if (percent < 60)      return m_hBrushGreen;
    else if (percent < 80) return m_hBrushYellow;
    else                   return m_hBrushRed;
}

// ============================================================================
// Settings
// ============================================================================

void FloatingWindow::SetOpacityPercent(int percent) {
    if (percent < OPACITY_MIN) percent = OPACITY_MIN;
    if (percent > OPACITY_MAX) percent = OPACITY_MAX;
    m_opacityPercent = percent;
    if (m_hWnd) {
        SetLayeredWindowAttributes(m_hWnd, 0, PercentToAlpha(percent), LWA_ALPHA);
    }
    SaveConfig();
}

void FloatingWindow::SetDisplayMode(int mode) {
    m_displayMode = mode;
    SaveConfig();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// ============================================================================
// Auto-start
// ============================================================================

void FloatingWindow::ToggleAutoStart() {
    bool enabled = IsAutoStartEnabled();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;

    if (enabled) {
        RegDeleteValueW(hKey, L"MemoryOptimizer");
    } else {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        RegSetValueExW(hKey, L"MemoryOptimizer", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(exePath),
                       static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
}

bool FloatingWindow::IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;

    DWORD type = 0;
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    LONG result = RegQueryValueExW(hKey, L"MemoryOptimizer", nullptr,
                                   &type, reinterpret_cast<LPBYTE>(path), &size);
    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS || type != REG_SZ) return false;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return (_wcsicmp(path, exePath) == 0);
}

// ============================================================================
// Opacity Slider Dialog
// ============================================================================

// Per-dialog data stored in GWLP_USERDATA
struct OpacityDlgData {
    FloatingWindow* pThis;
    HWND hSlider;
    HWND hValLabel;
    WNDPROC oldProc;
    bool closing;
};

// Custom window proc for the opacity dialog (replaces #32770 via SetWindowLongPtr)
static LRESULT CALLBACK OpacityDlgWndProc(HWND hWnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam) {
    OpacityDlgData* data = reinterpret_cast<OpacityDlgData*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!data) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            data->closing = true;
            EnableWindow(GetWindow(hWnd, GW_OWNER), TRUE);
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_HSCROLL:
        if (data->hSlider && reinterpret_cast<HWND>(lParam) == data->hSlider) {
            int pos = static_cast<int>(SendMessageW(data->hSlider, TBM_GETPOS, 0, 0));
            if (data->hValLabel) {
                wchar_t buf[32];
                swprintf_s(buf, 32, L"%d%%", pos);
                SetWindowTextW(data->hValLabel, buf);
            }
            if (data->pThis) data->pThis->SetOpacityPercent(pos);
        }
        return 0;

    case WM_CLOSE:
        data->closing = true;
        EnableWindow(GetWindow(hWnd, GW_OWNER), TRUE);
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        // Restore original wndproc before deleting data
        if (data->oldProc) {
            SetWindowLongPtrW(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(data->oldProc));
        }
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        delete data;
        return 0;
    }
    return CallWindowProcW(data->oldProc, hWnd, msg, wParam, lParam);
}

void FloatingWindow::ShowOpacityDialog() {
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"#32770", L"透明度设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 280, 120,
        m_hWnd, nullptr, m_hInstance, nullptr);

    if (!hDlg) return;

    // Alloc dialog data
    OpacityDlgData* data = new OpacityDlgData{};
    data->pThis   = this;
    data->closing = false;
    data->oldProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hDlg, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(OpacityDlgWndProc)));
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

    // Set font
    HFONT hDlgFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Microsoft YaHei UI");
    SendMessageW(hDlg, WM_SETFONT, reinterpret_cast<WPARAM>(hDlgFont), TRUE);

    // Label
    CreateWindowExW(0, L"Static", L"不透明度：",
                    WS_CHILD | WS_VISIBLE,
                    14, 14, 80, 18,
                    hDlg, nullptr, m_hInstance, nullptr);

    // TrackBar
    data->hSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    TBS_AUTOTICKS | TBS_TOOLTIPS,
                    14, 34, 195, 28,
                    hDlg, reinterpret_cast<HMENU>(IDC_SLIDER),
                    m_hInstance, nullptr);
    SendMessageW(data->hSlider, TBM_SETRANGE, TRUE, MAKELONG(OPACITY_MIN, OPACITY_MAX));
    SendMessageW(data->hSlider, TBM_SETPOS, TRUE, m_opacityPercent);
    SendMessageW(data->hSlider, TBM_SETTICFREQ, 5, 0);
    SendMessageW(data->hSlider, TBM_SETPAGESIZE, 0, 5);

    // Value label
    data->hValLabel = CreateWindowExW(0, L"Static", L"",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    215, 36, 50, 22,
                    hDlg, reinterpret_cast<HMENU>(IDC_LABEL_VAL),
                    m_hInstance, nullptr);
    {
        wchar_t buf[32];
        swprintf_s(buf, 32, L"%d%%", m_opacityPercent);
        SetWindowTextW(data->hValLabel, buf);
    }

    // OK button
    CreateWindowExW(0, L"Button", L"确定",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    190, 72, 70, 26,
                    hDlg, reinterpret_cast<HMENU>(IDOK),
                    m_hInstance, nullptr);

    // Center on owner
    {
        RECT rcOwner, rcDlg;
        GetWindowRect(m_hWnd, &rcOwner);
        GetWindowRect(hDlg, &rcDlg);
        int w = rcDlg.right - rcDlg.left;
        int h = rcDlg.bottom - rcDlg.top;
        SetWindowPos(hDlg, nullptr,
                     rcOwner.left + (rcOwner.right  - rcOwner.left - w) / 2,
                     rcOwner.top  + (rcOwner.bottom - rcOwner.top  - h) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    ShowWindow(hDlg, SW_SHOW);
    EnableWindow(m_hWnd, FALSE);

    // Modal message loop — only handles messages, exit when dialog destroys
    MSG dlgMsg = {};
    while (GetMessageW(&dlgMsg, nullptr, 0, 0)) {
        // Dispatch to subclassed dialog proc which sets data->closing
        TranslateMessage(&dlgMsg);
        DispatchMessageW(&dlgMsg);

        // Check if dialog was destroyed
        if (data->closing && !IsWindow(hDlg)) {
            break;
        }
    }

    // Ensure owner is re-enabled
    EnableWindow(m_hWnd, TRUE);
    SetForegroundWindow(m_hWnd);
    DeleteObject(hDlgFont);
}

// ============================================================================
// Self Trim
// ============================================================================

void FloatingWindow::SelfTrim() {
    SetProcessWorkingSetSize(GetCurrentProcess(),
                             static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));
}

// ============================================================================
// Window Creation
// ============================================================================

bool FloatingWindow::Create(HWND hParent) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_hBrushBg;
    wc.lpszClassName = CLASS_NAME;
    wc.style         = CS_DBLCLKS;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;

    if (!RegisterClassExW(&wc)) return false;

    RECT rcWork = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int x = rcWork.right  - WINDOW_SIZE - 16;
    int y = rcWork.bottom - WINDOW_SIZE - 56;

    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        CLASS_NAME, L"内存优化",
        WS_POPUP,
        x, y, WINDOW_SIZE, WINDOW_SIZE,
        hParent, nullptr, m_hInstance, this);

    if (!m_hWnd) return false;

    // Apply saved opacity
    SetLayeredWindowAttributes(m_hWnd, 0, PercentToAlpha(m_opacityPercent), LWA_ALPHA);

    // Clip window to rounded rectangle — eliminates black corners
    {
        HRGN hRgn = CreateRoundRectRgn(0, 0, WINDOW_SIZE + 1, WINDOW_SIZE + 1,
                                       CORNER_RADIUS, CORNER_RADIUS);
        SetWindowRgn(m_hWnd, hRgn, TRUE);
    }

    // Tooltip
    m_hTooltip = CreateWindowExW(
        0, TOOLTIPS_CLASSW, nullptr,
        TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hWnd, nullptr, m_hInstance, nullptr);

    if (m_hTooltip) {
        TOOLINFOW ti = {};
        ti.cbSize   = sizeof(TOOLINFOW);
        ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd     = m_hWnd;
        ti.uId      = reinterpret_cast<UINT_PTR>(m_hWnd);
        ti.lpszText = const_cast<LPWSTR>(L"内存优化 - 双击优化 右键菜单");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, 280);
    }

    AddTrayIcon();

    SetTimer(m_hWnd, IDT_MEMORY_REFRESH, 1500, nullptr);
    SetTimer(m_hWnd, IDT_SELF_TRIM, 30000, nullptr);

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    RefreshMemoryInfo();

    SelfTrim();
    return true;
}

// ============================================================================
// Message Loop
// ============================================================================

int FloatingWindow::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ============================================================================
// Window Procedure
// ============================================================================

LRESULT CALLBACK FloatingWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FloatingWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = static_cast<FloatingWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<FloatingWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }
    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Message Handler
// ============================================================================

LRESULT FloatingWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp();
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_RBUTTONUP:
        ShowTrayMenu();
        return 0;

    case WM_MOUSEWHEEL:
        {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int newPct = m_opacityPercent + (delta > 0 ? 5 : -5);
            SetOpacityPercent(newPct);
        }
        return 0;

    case WM_TIMER:
        if (wParam == IDT_MEMORY_REFRESH) {
            RefreshMemoryInfo();
            InvalidateRect(m_hWnd, nullptr, FALSE);
        } else if (wParam == IDT_SELF_TRIM) {
            SelfTrim();
        } else if (wParam == IDT_ANIMATION) {
            OnAnimationTimer();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(m_hWnd, IDT_MEMORY_REFRESH);
        KillTimer(m_hWnd, IDT_SELF_TRIM);
        KillTimer(m_hWnd, IDT_ANIMATION);
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    case WM_APP + 1: // tray callback
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDBLCLK:
            m_bVisible = !m_bVisible;
            ShowWindow(m_hWnd, m_bVisible ? SW_SHOW : SW_HIDE);
            if (m_bVisible) SetForegroundWindow(m_hWnd);
            break;
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_OPTIMIZE:     DoOptimize();                 break;
        case IDM_TRAY_SHOW:         m_bVisible = true; ShowWindow(m_hWnd, SW_SHOW); SetForegroundWindow(m_hWnd); break;
        case IDM_TRAY_HIDE:         m_bVisible = false; ShowWindow(m_hWnd, SW_HIDE); break;
        case IDM_TRAY_EXIT:         DestroyWindow(m_hWnd);        break;
        case IDM_DISPLAY_PERCENT:   SetDisplayMode(DISPLAY_PERCENT); break;
        case IDM_DISPLAY_USED:      SetDisplayMode(DISPLAY_USED);    break;
        case IDM_DISPLAY_AVAIL:     SetDisplayMode(DISPLAY_AVAIL);   break;
        case IDM_AUTOSTART_TOGGLE:  ToggleAutoStart();            break;
        case IDM_OPACITY_DIALOG:    ShowOpacityDialog();          break;
        }
        return 0;
    }
    return DefWindowProcW(m_hWnd, msg, wParam, lParam);
}

// ============================================================================
// Drawing
// ============================================================================

void FloatingWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hWnd, &ps);

    RECT rc;
    GetClientRect(m_hWnd, &rc);

    HDC hdcMem   = CreateCompatibleDC(hdc);
    HBITMAP hbm  = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbm));

    // Background rounded rect
    SelectObject(hdcMem, m_hBrushBg);
    SelectObject(hdcMem, m_hPenBorder);
    RoundRect(hdcMem, 1, 1, rc.right - 1, rc.bottom - 1,
              CORNER_RADIUS, CORNER_RADIUS);

    int percent = m_memInfo.memoryLoadPercent;
    if (percent > 100) percent = 100;

    COLORREF activeColor;
    if (m_bAnimating) {
        activeColor = m_animColor;
    } else if (percent < 60) {
        activeColor = RGB(72, 175, 76);
    } else if (percent < 80) {
        activeColor = RGB(250, 188, 5);
    } else {
        activeColor = RGB(235, 60, 50);
    }

    // Ring indicator
    int cx = rc.right / 2;
    int cy = rc.top + 14;
    int r  = 9;

    HPEN hColorPen = CreatePen(PS_SOLID | PS_ENDCAP_ROUND, 3, activeColor);

    SelectObject(hdcMem, m_hPenGray);
    SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
    Ellipse(hdcMem, cx - r, cy - r, cx + r, cy + r);

    SelectObject(hdcMem, hColorPen);
    if (percent > 0) {
        int endAngle = 90 - (percent * 360 / 100);
        double rad = (endAngle * 3.141592653589793) / 180.0;
        int ex = cx + static_cast<int>(r * cos(rad));
        int ey = cy - static_cast<int>(r * sin(rad));
        Pie(hdcMem, cx - r, cy - r, cx + r, cy + r,
            cx, cy - r, ex, ey);
    }
    DeleteObject(hColorPen);

    // Main text
    SetBkMode(hdcMem, TRANSPARENT);

    wchar_t mainText[16] = {};
    wchar_t unitText[8]  = {};

    switch (m_displayMode) {
    case DISPLAY_PERCENT:
        swprintf_s(mainText, 16, L"%d", percent);
        wcscpy_s(unitText, 8, L"%");
        break;
    case DISPLAY_USED:
        {
            double usedGB = static_cast<double>(m_memInfo.usedPhysicalKB * 1024)
                            / (1024.0 * 1024.0 * 1024.0);
            if (usedGB >= 10.0)
                swprintf_s(mainText, 16, L"%.0f", usedGB);
            else
                swprintf_s(mainText, 16, L"%.1f", usedGB);
            wcscpy_s(unitText, 8, L"G");
        }
        break;
    case DISPLAY_AVAIL:
        {
            double availGB = static_cast<double>(m_memInfo.availPhysicalKB * 1024)
                             / (1024.0 * 1024.0 * 1024.0);
            if (availGB >= 10.0)
                swprintf_s(mainText, 16, L"%.0f", availGB);
            else
                swprintf_s(mainText, 16, L"%.1f", availGB);
            wcscpy_s(unitText, 8, L"G");
        }
        break;
    }

    SelectObject(hdcMem, m_hFont);
    SetTextColor(hdcMem, activeColor);
    RECT rcNum = {0, cy + 3, rc.right, rc.bottom - 14};
    DrawTextW(hdcMem, mainText, -1, &rcNum,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, m_hFontSmall);
    RECT rcUnit = {0, cy + 19, rc.right, rc.bottom - 2};
    SetTextColor(hdcMem, RGB(155, 155, 160));
    DrawTextW(hdcMem, unitText, -1, &rcUnit,
              DT_CENTER | DT_TOP | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);

    EndPaint(m_hWnd, &ps);
}

// ============================================================================
// Tray Icon
// ============================================================================

void FloatingWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(NOTIFYICONDATAW);
    nid.hWnd             = m_hWnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy(nid.szTip, L"内存优化", _countof(nid.szTip) - 1);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void FloatingWindow::RemoveTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = m_hWnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ============================================================================
// Right-click / Tray Menu
// ============================================================================

static void AppendCheckMenu(HMENU menu, UINT id, const wchar_t* text, bool checked) {
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
}

void FloatingWindow::ShowTrayMenu() {
    HMENU hMenu = CreatePopupMenu();

    // Status line
    wchar_t statusBuf[128];
    swprintf_s(statusBuf, 128, L"内存 %d%%  已用 %s / %s",
               m_memInfo.memoryLoadPercent,
               FormatBytes(m_memInfo.usedPhysicalKB * 1024).c_str(),
               FormatBytes(m_memInfo.totalPhysicalKB * 1024).c_str());
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, statusBuf);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Optimize
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPTIMIZE, L"⚡ 优化内存");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Display mode submenu
    HMENU hDisplayMenu = CreatePopupMenu();
    AppendCheckMenu(hDisplayMenu, IDM_DISPLAY_PERCENT,
                    L"百分比 (%)", m_displayMode == DISPLAY_PERCENT);
    AppendCheckMenu(hDisplayMenu, IDM_DISPLAY_USED,
                    L"已用内存 (G)", m_displayMode == DISPLAY_USED);
    AppendCheckMenu(hDisplayMenu, IDM_DISPLAY_AVAIL,
                    L"可用内存 (G)", m_displayMode == DISPLAY_AVAIL);
    AppendMenuW(hMenu, MF_STRING | MF_POPUP,
                reinterpret_cast<UINT_PTR>(hDisplayMenu), L"显示模式");

    // Opacity — single item opens slider dialog, with current value
    wchar_t opacityItem[32];
    swprintf_s(opacityItem, 32, L"透明度设置...  (%d%%)", m_opacityPercent);
    AppendMenuW(hMenu, MF_STRING, IDM_OPACITY_DIALOG, opacityItem);

    // Auto-start
    AppendCheckMenu(hMenu, IDM_AUTOSTART_TOGGLE,
                    L"开机自启动", IsAutoStartEnabled());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Window toggle
    AppendMenuW(hMenu, MF_STRING, m_bVisible ? IDM_TRAY_HIDE : IDM_TRAY_SHOW,
                m_bVisible ? L"隐藏悬浮窗" : L"显示悬浮窗");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"退出");

    // Show
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);

    DestroyMenu(hDisplayMenu);
    DestroyMenu(hMenu);
}

// ============================================================================
// Memory Operations
// ============================================================================

void FloatingWindow::RefreshMemoryInfo() {
    m_memInfo = GetMemoryInfo();

    if (m_hTooltip) {
        wchar_t tipBuf[300];
        swprintf_s(tipBuf, 300,
            L"内存优化\r\n━━━━━━━━\r\n"
            L"已用: %s\r\n可用: %s\r\n总计: %s\r\n占用率: %d%%\r\n"
            L"━━━━━━━━\r\n"
            L"双击: 优化 | 右键: 菜单\r\n"
            L"滚轮: 调透明度 (%d%%)",
            FormatBytes(m_memInfo.usedPhysicalKB * 1024).c_str(),
            FormatBytes(m_memInfo.availPhysicalKB * 1024).c_str(),
            FormatBytes(m_memInfo.totalPhysicalKB * 1024).c_str(),
            m_memInfo.memoryLoadPercent,
            m_opacityPercent);

        TOOLINFOW ti = {};
        ti.cbSize   = sizeof(TOOLINFOW);
        ti.hwnd     = m_hWnd;
        ti.uId      = reinterpret_cast<UINT_PTR>(m_hWnd);
        ti.lpszText = tipBuf;
        SendMessageW(m_hTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

void FloatingWindow::DoOptimize() {
    OptimizeResult result = OptimizeAllProcesses();
    RefreshMemoryInfo();
    InvalidateRect(m_hWnd, nullptr, FALSE);

    if (m_hTooltip) {
        wchar_t msgBuf[200];
        if (result.bytesFreed > 0) {
            swprintf_s(msgBuf, 200,
                L"✓ 优化完成! 释放约 %s  (%d/%d 进程)",
                FormatBytes(result.bytesFreed).c_str(),
                result.successCount, result.processCount);
        } else {
            swprintf_s(msgBuf, 200,
                L"✓ 优化完成! 已刷新 %d 个进程的工作集",
                result.successCount);
        }
        TOOLINFOW ti = {};
        ti.cbSize   = sizeof(TOOLINFOW);
        ti.hwnd     = m_hWnd;
        ti.uId      = reinterpret_cast<UINT_PTR>(m_hWnd);
        ti.lpszText = msgBuf;
        SendMessageW(m_hTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }

    SelfTrim();
    StartOptimizeAnimation();
}

// ============================================================================
// Window Movement
// ============================================================================

void FloatingWindow::OnLButtonDown(int x, int y) {
    m_bDragging  = true;
    m_dragStartX = x;
    m_dragStartY = y;
    SetCapture(m_hWnd);
}

void FloatingWindow::OnMouseMove(int x, int y) {
    if (m_bDragging) {
        RECT rc;
        GetWindowRect(m_hWnd, &rc);
        SetWindowPos(m_hWnd, nullptr,
                     rc.left + (x - m_dragStartX),
                     rc.top  + (y - m_dragStartY),
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void FloatingWindow::OnLButtonUp() {
    if (m_bDragging) {
        m_bDragging = false;
        ReleaseCapture();

        POINT pt;
        GetCursorPos(&pt);
        RECT rc;
        GetWindowRect(m_hWnd, &rc);
        int dx = pt.x - rc.left - m_dragStartX;
        int dy = pt.y - rc.top  - m_dragStartY;
        if (abs(dx) < 4 && abs(dy) < 4) {
            // Fast double-click (within 450ms) triggers optimization
            DWORD now = GetTickCount();
            if (m_lastClickTime != 0 && (now - m_lastClickTime) < 450) {
                m_lastClickTime = 0;
                DoOptimize();
            } else {
                m_lastClickTime = now;
            }
        }
    }
}

// ============================================================================
// Animation
// ============================================================================

void FloatingWindow::StartOptimizeAnimation() {
    m_bAnimating = true;
    m_animFrame  = 0;
    m_animColor  = RGB(0, 230, 80);
    SetTimer(m_hWnd, IDT_ANIMATION, 80, nullptr);
}

void FloatingWindow::OnAnimationTimer() {
    m_animFrame++;
    if (m_animFrame >= 5) {
        m_bAnimating = false;
        KillTimer(m_hWnd, IDT_ANIMATION);
    } else {
        m_animColor = (m_animFrame % 2 == 0)
            ? RGB(0, 210, 70)
            : (m_memInfo.memoryLoadPercent < 60 ? RGB(72, 175, 76)
               : m_memInfo.memoryLoadPercent < 80 ? RGB(250, 188, 5)
               : RGB(235, 60, 50));
    }
    InvalidateRect(m_hWnd, nullptr, FALSE);
}
