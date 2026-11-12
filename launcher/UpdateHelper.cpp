#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cwchar>

namespace
{
void ShowError(const wchar_t* message, DWORD code)
{
    wchar_t text[320];
    swprintf_s(text, L"%ls (Windows error %lu).", message, code);
    MessageBoxW(nullptr, text, L"LCEReborn Live update", MB_OK | MB_ICONERROR);
}

bool Launch(const wchar_t* file, const wchar_t* arguments, HANDLE* process)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = file;
    info.lpParameters = arguments;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
        return false;
    *process = info.hProcess;
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 4)
    {
        if (args) LocalFree(args);
        return 1;
    }
    const wchar_t* installer = args[1];
    const wchar_t* launcher = args[2];
    const DWORD parentPid = wcstoul(args[3], nullptr, 10);
    if (!parentPid || GetFileAttributesW(installer) == INVALID_FILE_ATTRIBUTES)
    {
        LocalFree(args);
        return 1;
    }

    // The installer must not replace the launcher while that process is alive.
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (parent)
    {
        const DWORD wait = WaitForSingleObject(parent, 60000);
        CloseHandle(parent);
        if (wait != WAIT_OBJECT_0)
        {
            ShowError(L"The launcher did not close before updating", wait);
            LocalFree(args);
            return 1;
        }
    }

    HANDLE setup = nullptr;
    DWORD error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (Launch(installer, L"/SILENT /NORESTART", &setup))
            break;
        error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
            break;
        Sleep(500);
    }
    if (!setup)
    {
        ShowError(L"Windows could not start the installer", error);
        LocalFree(args);
        return 1;
    }

    WaitForSingleObject(setup, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(setup, &exitCode);
    CloseHandle(setup);
    if (exitCode != 0)
        ShowError(L"The installer did not finish successfully", exitCode);

    HANDLE restarted = nullptr;
    if (!Launch(launcher, nullptr, &restarted))
        ShowError(L"Windows could not reopen the launcher", GetLastError());
    if (restarted) CloseHandle(restarted);
    DeleteFileW(installer);
    LocalFree(args);
    return exitCode == 0 ? 0 : 1;
}
