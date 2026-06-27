// Memory Optimizer — Standalone Windows memory optimization tool
//
// Inspired by PCL (Plain Craft Launcher) 百宝箱 memory optimization feature.
// Uses the same EmptyWorkingSet technique as PCL to trim process working sets.
//
// Build:
//   cmake -B build -G "Visual Studio 17 2022" -A x64
//   cmake --build build --config Release
//
// Usage:
//   MemoryOptimizer.exe          → opens floating window + tray icon
//   MemoryOptimizer.exe --memory → silent optimization, exits with KB freed
//
// Memory footprint of this app: ~500KB private working set (after self-trim).

#include <windows.h>
#include <commctrl.h>
#include <cstring>
#include "FloatingWindow.h"

// ============================================================================
// WinMain
// ============================================================================

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPSTR     lpCmdLine,
    int       /*nShowCmd*/)
{
    // Init common controls (tooltip)
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    // ---- Silent mode (--memory) ----
    // Performs optimization and exits, like PCL's --memory flag.
    if (lpCmdLine && strstr(lpCmdLine, "--memory")) {
        EnableDebugPrivilege();
        OptimizeResult result = OptimizeAllProcesses();

        // Trim our own footprint before exit
        SetProcessWorkingSetSize(GetCurrentProcess(),
                                 static_cast<SIZE_T>(-1),
                                 static_cast<SIZE_T>(-1));

        // Return KB freed as exit code (matching PCL behavior)
        int exitCode = static_cast<int>(result.bytesFreed / 1024);
        return (exitCode > 0) ? exitCode : 0;
    }

    // ---- Single-instance check ----
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MemoryOptimizer_Singleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(FloatingWindow::CLASS_NAME, nullptr);
        if (hExisting) {
            SetForegroundWindow(hExisting);
            ShowWindow(hExisting, SW_SHOW);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // ---- GUI mode ----
    FloatingWindow app(hInstance);

    if (!app.Create()) {
        MessageBoxW(nullptr,
            L"无法创建悬浮窗。\n请检查系统环境。",
            L"内存优化 - 错误",
            MB_OK | MB_ICONERROR);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 1;
    }

    // Self-trim early (most heap init done, release the temp allocations)
    SetProcessWorkingSetSize(GetCurrentProcess(),
                             static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));

    int result = app.Run();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return result;
}
