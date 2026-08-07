#pragma once

#include "App.xaml.g.h"

namespace winrt::winui::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    
        inline static winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}
