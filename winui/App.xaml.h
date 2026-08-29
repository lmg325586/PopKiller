#pragma once

#include "App.xaml.g.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <string>

namespace winrt::winui::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        winrt::Microsoft::Windows::AppLifecycle::AppInstance m_keyInstance{ nullptr };

        inline static Microsoft::UI::Xaml::Window window{ nullptr };
        
        // 用于跨页面传递设置页的聚焦目标
        inline static std::wstring PendingSettingsFocus;
    };
}