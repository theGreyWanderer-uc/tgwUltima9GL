#include <windows.h>
#include "u9gl_version.h"
#include <stdio.h>
#include <string.h>

static int inject_into(HANDLE proc, const char *dllPath)
{
    SIZE_T  len   = strlen(dllPath) + 1;
    LPVOID  remote;
    HMODULE k32   = GetModuleHandleA("kernel32.dll");
    FARPROC loadLib = GetProcAddress(k32, "LoadLibraryA");
    HANDLE  thread;
    DWORD   exitCode = 0;

    if (!loadLib) { fprintf(stderr, "no LoadLibraryA\n"); return 0; }

    remote = VirtualAllocEx(proc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { fprintf(stderr, "VirtualAllocEx failed: %lu\n", GetLastError()); return 0; }

    if (!WriteProcessMemory(proc, remote, dllPath, len, NULL)) {
        fprintf(stderr, "WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return 0;
    }

    thread = CreateRemoteThread(proc, NULL, 0,
                                (LPTHREAD_START_ROUTINE)loadLib, remote, 0, NULL);
    if (!thread) {
        fprintf(stderr, "CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return 0;
    }
    WaitForSingleObject(thread, 10000);
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);

    if (exitCode == 0) {
        fprintf(stderr, "LoadLibraryA returned NULL in the target "
                        "(wrong architecture, or a dependency is missing)\n");
        return 0;
    }
    printf("injected, module base in target = 0x%08lX\n", exitCode);
    return 1;
}

int main(int argc, char **argv)
{
    char dllPath[MAX_PATH];
    char exePath[MAX_PATH] = "";
    DWORD attachPid = 0;
    int i;

    GetModuleFileNameA(NULL, dllPath, sizeof dllPath);
    { char *slash = strrchr(dllPath, '\\'); if (slash) slash[1] = 0; }
    strncat(dllPath, "u9log.dll", sizeof dllPath - strlen(dllPath) - 1);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--attach") && i + 1 < argc)      attachPid = (DWORD)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dll") && i + 1 < argc)    { strncpy(dllPath, argv[++i], MAX_PATH-1); dllPath[MAX_PATH-1]=0; }
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc)    { strncpy(exePath, argv[++i], MAX_PATH-1); exePath[MAX_PATH-1]=0; }
        else { fprintf(stderr, "unknown argument: %s\n", argv[i]); return 2; }
    }
    if (!attachPid && !exePath[0]) {
        fprintf(stderr, "usage: u9inject.exe --exe <path to u9.exe> [--dll path]\n"
                        "       u9inject.exe --attach PID [--dll path]\n");
        return 2;
    }

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "DLL not found: %s\n", dllPath);
        return 2;
    }
    printf("u9inject %s\n", U9GL_VERSION_FULL);
    printf("dll: %s\n", dllPath);

    if (attachPid) {
        HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, attachPid);
        int ok;
        if (!proc) { fprintf(stderr, "OpenProcess(%lu) failed: %lu\n", attachPid, GetLastError()); return 1; }
        printf("attaching to pid %lu\n", attachPid);
        ok = inject_into(proc, dllPath);
        CloseHandle(proc);
        return ok ? 0 : 1;
    } else {
        STARTUPINFOA si; PROCESS_INFORMATION pi; char dir[MAX_PATH]; char *slash;
        int ok;
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        memset(&pi, 0, sizeof pi);

        strncpy(dir, exePath, sizeof dir - 1); dir[sizeof dir - 1] = 0;
        slash = strrchr(dir, '\\'); if (slash) *slash = 0;

        printf("launching %s (suspended)\n", exePath);
        if (!CreateProcessA(exePath, NULL, NULL, NULL, FALSE,
                            CREATE_SUSPENDED, NULL, dir, &si, &pi)) {
            fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
            return 1;
        }
        ok = inject_into(pi.hProcess, dllPath);
        ResumeThread(pi.hThread);
        printf("resumed, pid %lu\n", pi.dwProcessId);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return ok ? 0 : 1;
    }
}
