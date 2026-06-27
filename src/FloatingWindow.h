#pragma once
#include <windows.h>
#include "MemoryOptimizer.h"
#include "resource.h"

class FloatingWindow {
public:
    static const wchar_t* CLASS_NAME;

    FloatingWindow(HINSTANCE hInstance);
    ~FloatingWindow();

    bool Create(HWND hParent = nullptr);
    HWND GetHwnd() const { return m_hWnd; }
    int Run();

    // Settings (public for dialog subclass callback access)
    void SetOpacityPercent(int percent);  // 15-100, clamps internally
    void SetDisplayMode(int mode);
    void ToggleAutoStart();
    bool IsAutoStartEnabled();

    // Opacity slider dialog
    void ShowOpacityDialog();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Drawing
    void OnPaint();
    void CreateUIBrushes();
    void CleanupUIBrushes();
    HBRUSH GetColorBrush(int percent);

    // Tray
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    // Memory
    void RefreshMemoryInfo();
    void DoOptimize();
    void SelfTrim();

    // Window movement
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnLButtonUp();

    // Animation
    void StartOptimizeAnimation();
    void OnAnimationTimer();

    // Config I/O
    void LoadConfig();
    void SaveConfig();

    HINSTANCE m_hInstance;
    HWND     m_hWnd;
    HWND     m_hTooltip;

    // GDI resources
    HFONT    m_hFont;
    HFONT    m_hFontSmall;
    HBRUSH   m_hBrushBg;
    HBRUSH   m_hBrushGreen;
    HBRUSH   m_hBrushYellow;
    HBRUSH   m_hBrushRed;
    HPEN     m_hPenBorder;
    HPEN     m_hPenGray;

    // State
    bool     m_bVisible;
    bool     m_bDragging;
    int      m_dragStartX;
    int      m_dragStartY;
    DWORD    m_lastClickTime;   // for double-click detection
    MemoryInfo m_memInfo;

    // Animation
    bool     m_bAnimating;
    int      m_animFrame;
    COLORREF m_animColor;

    // Settings
    int      m_opacityPercent;  // 15-100
    int      m_displayMode;

    static constexpr int WINDOW_SIZE   = 56;
    static constexpr int CORNER_RADIUS = 10;
};
