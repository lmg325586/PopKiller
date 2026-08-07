#pragma once

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
        auto bg = mica ? ui::Colors::Transparent() : ui::Colors::White();

        titleBar.BackgroundColor(bg);
        titleBar.InactiveBackgroundColor(bg);
        titleBar.ButtonBackgroundColor(bg);
        titleBar.ButtonInactiveBackgroundColor(bg);

        titleBar.ForegroundColor(ui::Colors::Black());
        titleBar.InactiveForegroundColor(ui::Colors::Gray());
        titleBar.ButtonForegroundColor(ui::Colors::Black());
        titleBar.ButtonInactiveForegroundColor(ui::Colors::Gray());
        titleBar.ButtonHoverBackgroundColor(ui::Colors::LightGray());
        titleBar.ButtonHoverForegroundColor(ui::Colors::Black());
        titleBar.ButtonPressedBackgroundColor(ui::Colors::LightGray());
        titleBar.ButtonPressedForegroundColor(ui::Colors::Black());

        if (TitleBarElement)
        {
            TitleBarElement.Background(mux::Media::SolidColorBrush{ bg });
        }
    }
}