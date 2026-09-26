#pragma once
#include <windows.h>

// 让 Win32 原生弹出菜单（托盘右键菜单等）跟随系统深色模式。
// uxtheme.dll 以序号导出这些未公开 API，故动态加载。
namespace DarkMode
{
    enum class PreferredAppMode
    {
        Default,
        AllowDark,
        ForceDark,
        ForceLight,
        Max
    };

    using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode); // ordinal 135
    using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);                // ordinal 133（旧）
    using AllowDarkModeForWindowExFn = bool(WINAPI*)(HWND, bool);              // ordinal 145（Win11，随主题自动切换）
    using FlushMenuThemesFn = void(WINAPI*)();                                 // ordinal 136

    inline SetPreferredAppModeFn SetPreferredAppMode{};
    inline AllowDarkModeForWindowFn AllowDarkModeForWindow{};
    inline AllowDarkModeForWindowExFn AllowDarkModeForWindowEx{};
    inline FlushMenuThemesFn FlushMenuThemes{};
    inline bool Loaded = false;

    inline void Load()
    {
        if (Loaded) return;

        HMODULE ux = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!ux) return; // 加载失败不置 Loaded，下次可重试

        SetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
            ::GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        AllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(
            ::GetProcAddress(ux, MAKEINTRESOURCEA(133)));
        AllowDarkModeForWindowEx = reinterpret_cast<AllowDarkModeForWindowExFn>(
            ::GetProcAddress(ux, MAKEINTRESOURCEA(145)));
        FlushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(
            ::GetProcAddress(ux, MAKEINTRESOURCEA(136)));

        // 句柄常驻：函数指针在其卸载后会失效，故不 FreeLibrary
        Loaded = true;
    }

    // 必须在创建任何窗口之前调用一次，弹出菜单才会跟随系统主题。
    inline void Init()
    {
        Load();
        if (SetPreferredAppMode) SetPreferredAppMode(PreferredAppMode::AllowDark);
    }

    // 窗口创建后调用：让原生控件也启用深色（随主题自动切换）。
    inline void ApplyToWindow(HWND hwnd)
    {
        if (!hwnd) return;
        if (AllowDarkModeForWindowEx) AllowDarkModeForWindowEx(hwnd, true);
        else if (AllowDarkModeForWindow) AllowDarkModeForWindow(hwnd, TRUE);
    }

    // 系统主题变化时刷新已缓存的主题，使下次弹出菜单即时生效。
    inline void RefreshMenus()
    {
        if (FlushMenuThemes) FlushMenuThemes();
    }
}
