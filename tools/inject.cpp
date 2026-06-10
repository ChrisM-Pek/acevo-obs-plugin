// Minimal injector: loads acevo_obs.dll into AssettoCorsaEVO.exe via
// CreateRemoteThread(LoadLibraryW).
//
// Usage: inject.exe <absolute_path_to_dll> [process.exe]
//
// Run as ADMINISTRATOR, game must already be running.
// Legacy tool — prefer the dxgi.dll proxy for early hook installation.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>

static DWORD FindPid(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        wprintf(L"Usage: inject.exe <dll> [process.exe]\n");
        return 1;
    }
    const wchar_t* dll = argv[1];
    const wchar_t* proc = (argc >= 3) ? argv[2] : L"AssettoCorsaEVO.exe";

    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"DLL not found: %s\n", dll);
        return 1;
    }

    DWORD pid = FindPid(proc);
    if (!pid) { wprintf(L"Process %s not found (is the game running?)\n", proc); return 1; }
    wprintf(L"Process %s PID=%lu\n", proc, pid);

    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                           PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                           FALSE, pid);
    if (!h) { wprintf(L"OpenProcess failed (run as admin?) err=%lu\n", GetLastError()); return 1; }

    SIZE_T bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(h, remote, dll, bytes, nullptr);

    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE th = CreateRemoteThread(h, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) { wprintf(L"CreateRemoteThread failed err=%lu\n", GetLastError()); return 1; }

    WaitForSingleObject(th, INFINITE);
    DWORD exitCode = 0; GetExitCodeThread(th, &exitCode);
    wprintf(L"Injection OK (LoadLibrary -> 0x%lX)\n", exitCode);

    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(h);
    return 0;
}
