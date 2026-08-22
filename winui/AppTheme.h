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