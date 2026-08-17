#pragma once
#include <windows.h>
#include <string>

namespace AppSettings
{
    inline std::wstring IniPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring dir(path);
        auto pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) dir = dir.substr(0, pos);
        return dir + L"\\winui.ini";
    }

    inline void Flush()
    {
        ::WritePrivateProfileStringW(nullptr, nullptr, nullptr, IniPath().c_str());
    }

    inline int ReadInt(const wchar_t* section, const wchar_t* key, int def)
    {
        return ::GetPrivateProfileIntW(section, key, def, IniPath().c_str());
    }

    inline void WriteInt(const wchar_t* section, const wchar_t* key, int value)
    {
        ::WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), IniPath().c_str());
        Flush();
    }

    inline std::wstring ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& def = L"")
    {
        WCHAR buffer[4096]{};
        ::GetPrivateProfileStringW(section, key, def.c_str(), buffer, 4096, IniPath().c_str());
        return buffer;
    }

    inline void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value)
    {
        ::WritePrivateProfileStringW(section, key, value.c_str(), IniPath().c_str());
        Flush();
    }

    inline void DeleteKey(const wchar_t* section, const wchar_t* key)
    {
        ::WritePrivateProfileStringW(section, key, nullptr, IniPath().c_str());
        Flush();
    }
}