// CleanupCA.cpp - uninstall cleanup custom action:
//   * delete leftover autostart values  HKCU/HKLM ...\Run\PopKiller
//   * delete runtime data files in the install folder (dir passed via CustomActionData)
// Avoids <msi.h>; declares the single MSI API used and links msi.lib.
#include <windows.h>
#include <string>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "msi.lib")

typedef unsigned long MSIHANDLE;
extern "C" UINT WINAPI MsiGetPropertyW(MSIHANDLE hInstall, LPCWSTR szName, LPWSTR szValueBuf, LPDWORD pchValueBuf);

static void DeleteRunValue(HKEY root, const wchar_t* subkey)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS)
    {
        RegDeleteValueW(k, L"PopKiller");
        RegCloseKey(k);
    }
}

static std::wstring Join(const std::wstring& dir, const wchar_t* name)
{
    std::wstring p = dir;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    p += name;
    return p;
}

static void DeleteMask(const std::wstring& dir, const wchar_t* mask)
{
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(Join(dir, mask).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::wstring full = Join(dir, fd.cFileName);
            ::SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            ::DeleteFileW(full.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void DeleteTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring full = Join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(full);
            else { ::SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL); ::DeleteFileW(full.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

static void DeleteStartupShortcuts()
{
    const wchar_t* sub = L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\PopKiller.lnk";
    wchar_t base[1024]{};
    if (GetEnvironmentVariableW(L"APPDATA", base, ARRAYSIZE(base)))
        ::DeleteFileW((std::wstring(base) + sub).c_str());
    if (GetEnvironmentVariableW(L"ProgramData", base, ARRAYSIZE(base)))
        ::DeleteFileW((std::wstring(base) + sub).c_str());
}

extern "C" __declspec(dllexport) UINT __stdcall DeleteLeftovers(MSIHANDLE hInstall)
{
    // 1) autostart registry values
    DeleteRunValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    DeleteRunValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");

    // 2) leftover autostart shortcuts (older builds used the Startup folder)
    DeleteStartupShortcuts();

    // 2) runtime data files under the install directory
    wchar_t buf[32768]{};
    DWORD n = ARRAYSIZE(buf);
    if (MsiGetPropertyW(hInstall, L"CustomActionData", buf, &n) == ERROR_SUCCESS && n > 0)
    {
        std::wstring dir = buf;
        const wchar_t* files[] = {
            L"rules.json", L"rules.json.bak", L"winui.ini", L"blocklog.txt",
            L"labels.json", L"crash.log", L"autostart_debug.log", L"notifydiag.txt"
        };
        for (auto f : files)
        {
            std::wstring p = Join(dir, f);
            ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            ::DeleteFileW(p.c_str());
        }
        DeleteMask(dir, L"*.dmp");
        DeleteMask(dir, L"*.log");
        DeleteTree(Join(dir, L"StaticML"));
    }
    return ERROR_SUCCESS;
}
