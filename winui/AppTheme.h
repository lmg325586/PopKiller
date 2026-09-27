#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.h>

namespace AppTheme
{
    inline int32_t Index{ 0 };
    inline winrt::Microsoft::UI::Xaml::Controls::Panel TitleBarElement{ nullptr };

    inline void ApplyTitleBar(winrt::Microsoft::UI::Windowing::AppWindowTitleBar const& titleBar)
    {
        namespace ui = winrt::Windows::UI;
        namespace mux = winrt::Microsoft::UI::Xaml;

        bool mica = (Index == 1);
        bool dark = TitleBarElement &&
            (TitleBarElement.ActualTheme() == mux::ElementTheme::Dark);
        auto bg = mica ? ui::Colors::Transparent()
            : (dark ? ui::Colors::Black() : ui::Colors::White());

        titleBar.BackgroundColor(bg);
        titleBar.InactiveBackgroundColor(bg);

        auto fg = dark ? ui::Colors::White() : ui::Colors::Black();
        auto inactiveFg = dark ? ui::Color{ 0xFF, 0x9E, 0x9E, 0x9E } : ui::Colors::Gray();
        auto hoverBg = dark ? ui::Color{ 0x1A, 0xFF, 0xFF, 0xFF }
        : ui::Color{ 0x14, 0x00, 0x00, 0x00 };
        auto pressedBg = dark ? ui::Color{ 0x33, 0xFF, 0xFF, 0xFF }
        : ui::Color{ 0x24, 0x00, 0x00, 0x00 };

        titleBar.ForegroundColor(fg);
        titleBar.InactiveForegroundColor(inactiveFg);
        titleBar.ButtonBackgroundColor(ui::Colors::Transparent());
        titleBar.ButtonInactiveBackgroundColor(ui::Colors::Transparent());
        titleBar.ButtonForegroundColor(fg);
        titleBar.ButtonInactiveForegroundColor(inactiveFg);
        titleBar.ButtonHoverBackgroundColor(hoverBg);
        titleBar.ButtonHoverForegroundColor(fg);
        titleBar.ButtonPressedBackgroundColor(pressedBg);
        titleBar.ButtonPressedForegroundColor(fg);

        if (TitleBarElement)
        {
            TitleBarElement.Background(mux::Media::SolidColorBrush{ bg });
        }
    }
}

// ---- 原生弹出菜单深色模式（原 DarkMode.h）----
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