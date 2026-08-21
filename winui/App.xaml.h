#pragma once

#include "App.xaml.g.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>

namespace winrt::winui::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        winrt::Microsoft::Windows::AppLifecycle::AppInstance m_keyInstance{ nullptr };

        inline static Microsoft::UI::Xaml::Window window{ nullptr };
    };
}