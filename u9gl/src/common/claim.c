#include <windows.h>
#include "claim.h"

int u9claim_process(void)
{
    char name[64];
    HANDLE m;

    wsprintfA(name, "Local\\u9gl_attach_%lu", GetCurrentProcessId());
    m = CreateMutexA(NULL, FALSE, name);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        OutputDebugStringA("u9gl: another copy is already attached to this process - this one stays passive\n");
        return 0;
    }
    return 1;
}
