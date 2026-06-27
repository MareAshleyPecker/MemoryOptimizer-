#pragma once
#include <windows.h>
#include <string>

struct MemoryInfo {
    DWORDLONG totalPhysicalKB;
    DWORDLONG availPhysicalKB;
    DWORDLONG usedPhysicalKB;
    DWORD     memoryLoadPercent;  // 0-100
};

struct OptimizeResult {
    DWORD processCount;
    DWORD successCount;
    DWORD failCount;
    SIZE_T bytesFreed;       // approximate
};

// Get current system memory information
MemoryInfo GetMemoryInfo();

// Optimize memory by trimming working sets of all accessible processes
// Requires SeDebugPrivilege for maximum effectiveness
OptimizeResult OptimizeAllProcesses();

// Optimize memory of a single process by handle
bool OptimizeProcessMemory(HANDLE hProcess);

// Enable SeDebugPrivilege for the current process
bool EnableDebugPrivilege();

// Format bytes to human-readable string (e.g., "1.5 GB")
std::wstring FormatBytes(DWORDLONG bytes);
