#pragma once
#pragma comment(lib, "advapi32.lib")
#include <windows.h>
#include <string>
#include <cstdio>

namespace AutoStart
{
    inline const wchar_t* RunKeyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    inline const wchar_t* ApprovedPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    inline const wchar_t* ValueName = L"PopKiller";

    inline void DebugLog(const wchar_t* msg)
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        p = p.substr(0, pos + 1) + L"autostart_debug.log";
        WCHAR user[64]{}; DWORD ulen = 64;
        ::GetUserNameW(user, &ulen);
        FILE* f{};
        if (_wfopen_s(&f, p.c_str(), L"a") == 0 && f)
        {
            SYSTEMTIME st{}; ::GetLocalTime(&st);
            fwprintf(f, L"%02d:%02d:%02d user=%s %s\n",
                st.wHour, st.wMinute, st.wSecond, user, msg);
            ::fclose(f);
        }
    }

    inline std::wstring GetExePathQuoted()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::wstring(L"\"") + path + L"\" --autostart";
    }

    inline bool IsEnabled()
    {
        HKEY hKeyRun{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_READ, &hKeyRun) != ERROR_SUCCESS)
            return false;
        bool exists = (::RegQueryValueExW(hKeyRun, ValueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
        ::RegCloseKey(hKeyRun);
        if (!exists) return false;

        HKEY hApproved{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, ApprovedPath, 0, KEY_READ, &hApproved) == ERROR_SUCCESS)
        {
            BYTE data[12]{};
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            if (::RegQueryValueExW(hApproved, ValueName, nullptr, &type, data, &dataSize) == ERROR_SUCCESS)
            {
                ::RegCloseKey(hApproved);
                return !(data[0] == 0x01 || data[0] == 0x03);
            }
            ::RegCloseKey(hApproved);
        }
        return true;
    }

    inline bool EnableAutoStartup()
    {
        DebugLog(L"Enable enter");
        HKEY hKey{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        {
            DebugLog(L"Enable FAILED open");
            return false;
        }
        std::wstring exePath = GetExePathQuoted();
        LONG result = ::RegSetValueExW(hKey, ValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exePath.c_str()),
            static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(hKey);
        if (result != ERROR_SUCCESS)
        {
            DebugLog(L"Enable FAILED write");
            return false;
        }

        HKEY hApproved{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, ApprovedPath, 0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS)
        {
            ::RegDeleteValueW(hApproved, ValueName);
            ::RegCloseKey(hApproved);
        }
        DebugLog(L"Enable write ok");
        return true;
    }

    inline bool DisableAutoStartup()
    {
        DebugLog(L"Disable enter");
        HKEY hKey{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
            return false;
        LONG result = ::RegDeleteValueW(hKey, ValueName);
        ::RegCloseKey(hKey);
        DebugLog(result == ERROR_SUCCESS ? L"Disable deleted" : L"Disable noop");
        return (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    inline void SyncPath()
    {
        if (IsEnabled()) EnableAutoStartup();
    }
}