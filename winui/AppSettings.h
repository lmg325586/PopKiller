#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <unordered_map>

// 应用设置持久化：读写 exe 同目录下的 winui.ini。
namespace AppSettings
{
    // 读操作优先走内存缓存，避免每次 GetPrivateProfileIntW 读盘
    inline std::mutex& CacheMutex()
    {
        static std::mutex m;
        return m;
    }
    inline std::unordered_map<std::wstring, int>& IntCache()
    {
        static std::unordered_map<std::wstring, int> c;
        return c;
    }
    inline std::wstring CacheKey(const wchar_t* section, const wchar_t* key)
    {
        return std::wstring(section) + L'|' + key;
    }
    inline constexpr size_t kIntCacheMax = 256;

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

    // 读取 [section] key 的整数，缺失或非法时返回 def（内存缓存）。
    inline int ReadInt(const wchar_t* section, const wchar_t* key, int def)
    {
        std::wstring k = CacheKey(section, key);
        {
            std::lock_guard<std::mutex> l(CacheMutex());
            auto it = IntCache().find(k);
            if (it != IntCache().end()) return it->second;
        }
        int v = ::GetPrivateProfileIntW(section, key, def, IniPath().c_str());
        {
            std::lock_guard<std::mutex> l(CacheMutex());
            auto& c = IntCache();
            if (c.size() > kIntCacheMax) c.clear();
            c[k] = v;
        }
        return v;
    }

    // 写入 [section] key=value 并立即 Flush（同步更新缓存）。
    inline void WriteInt(const wchar_t* section, const wchar_t* key, int value)
    {
        ::WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), IniPath().c_str());
        Flush();
        std::lock_guard<std::mutex> l(CacheMutex());
        IntCache()[CacheKey(section, key)] = value;
    }
}