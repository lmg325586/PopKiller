#pragma once
#include <windows.h>
#include <string>

// 应用设置持久化：读写 exe 同目录下的 winui.ini。
namespace AppSettings
{
    // 返回 exe 同目录下 winui.ini 的完整路径。
    inline std::wstring IniPath()
    {
        std::wstring dir = GetSelfPath();
        auto pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) dir = dir.substr(0, pos);
        return dir + L"\\winui.ini";
    }

    // 将 INI 写盘缓存刷回文件（系统默认延迟落盘，这里强制一次）。
    inline void Flush()
    {
        ::WritePrivateProfileStringW(nullptr, nullptr, nullptr, IniPath().c_str());
    }

    // 读取 [section] key 的整数，缺失或非法时返回 def。
    inline int ReadInt(const wchar_t* section, const wchar_t* key, int def)
    {
        return ::GetPrivateProfileIntW(section, key, def, IniPath().c_str());
    }

    // 写入 [section] key=value 并立即 Flush。
    inline void WriteInt(const wchar_t* section, const wchar_t* key, int value)
    {
        ::WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), IniPath().c_str());
        Flush();
    }
}