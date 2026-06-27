#include "MemoryOptimizer.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <sstream>
#include <iomanip>

// Link psapi for EmptyWorkingSet
#pragma comment(lib, "psapi.lib")

MemoryInfo GetMemoryInfo() {
    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);

    MemoryInfo info = {};
    info.totalPhysicalKB = memStatus.ullTotalPhys / 1024;
    info.availPhysicalKB = memStatus.ullAvailPhys / 1024;
    info.usedPhysicalKB = info.totalPhysicalKB - info.availPhysicalKB;
    info.memoryLoadPercent = memStatus.dwMemoryLoad;
    return info;
}

bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }

    bool success = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return success;
}

bool OptimizeProcessMemory(HANDLE hProcess) {
    // EmptyWorkingSet removes as many pages as possible from the working set
    // This is the key API used by PCL and similar memory optimizers
    return EmptyWorkingSet(hProcess) != FALSE;
}

OptimizeResult OptimizeAllProcesses() {
    OptimizeResult result = {};
    result.bytesFreed = 0;

    // Get memory before optimization
    MEMORYSTATUSEX beforeMem = {};
    beforeMem.dwLength = sizeof(beforeMem);
    GlobalMemoryStatusEx(&beforeMem);
    DWORDLONG availBefore = beforeMem.ullAvailPhys;

    // Try to enable debug privilege for better access
    EnableDebugPrivilege();

    // Take a snapshot of all processes
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W pe32 = {};
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            result.processCount++;

            // Skip system idle process (PID 0) and system process (PID 4)
            if (pe32.th32ProcessID == 0) continue;

            HANDLE hProcess = OpenProcess(
                PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION,
                FALSE, pe32.th32ProcessID);

            if (hProcess != nullptr) {
                if (OptimizeProcessMemory(hProcess)) {
                    result.successCount++;
                } else {
                    result.failCount++;
                }
                CloseHandle(hProcess);
            } else {
                // Some system processes cannot be opened — that's expected
                result.failCount++;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);

    // Calculate approximate bytes freed
    MEMORYSTATUSEX afterMem = {};
    afterMem.dwLength = sizeof(afterMem);
    GlobalMemoryStatusEx(&afterMem);
    DWORDLONG availAfter = afterMem.ullAvailPhys;

    if (availAfter > availBefore) {
        result.bytesFreed = availAfter - availBefore;
    }

    return result;
}

std::wstring FormatBytes(DWORDLONG bytes) {
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (bytes >= 1024ULL * 1024 * 1024) {
        oss << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << L" GB";
    } else if (bytes >= 1024 * 1024) {
        oss << static_cast<double>(bytes) / (1024.0 * 1024.0) << L" MB";
    } else if (bytes >= 1024) {
        oss << static_cast<double>(bytes) / 1024.0 << L" KB";
    } else {
        oss << bytes << L" B";
    }
    return oss.str();
}
