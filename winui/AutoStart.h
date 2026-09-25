#pragma once
#pragma comment(lib, "advapi32.lib")
#include <windows.h>
#include <string>

namespace AutoStart
{
    inline const wchar_t* RunKeyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    inline const wchar_t* ApprovedPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    inline const wchar_t* ValueName = L"PopKiller";

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
        HKEY hKey{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        {
            return false;
        }
        std::wstring exePath = GetExePathQuoted();
        LONG result = ::RegSetValueExW(hKey, ValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exePath.c_str()),
            static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(hKey);
        if (result != ERROR_SUCCESS)
        {
            return false;
        }

        HKEY hApproved{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, ApprovedPath, 0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS)
        {
            ::RegDeleteValueW(hApproved, ValueName);
            ::RegCloseKey(hApproved);
        }
        return true;
    }

    inline bool DisableAutoStartup()
    {
        HKEY hKey{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
            return false;
        LONG result = ::RegDeleteValueW(hKey, ValueName);
        ::RegCloseKey(hKey);
        return (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    inline void SyncPath()
    {
        if (IsEnabled()) EnableAutoStartup();
    }
}