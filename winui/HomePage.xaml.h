#pragma once

#include "HomePage.g.h"

namespace winrt::winui::implementation
{
    struct HomePage : HomePageT<HomePage>
    {
        HomePage();
        ~HomePage();

        void RootPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void RootPointerExited(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void GoToSettings_Tapped(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void GoToBlocker_Tapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args);

        void EnableEngine_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void UpdateGlow(winrt::Microsoft::UI::Xaml::Controls::Border const& card,
            winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas,
            winrt::Microsoft::UI::Xaml::Shapes::Ellipse const& glow,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

        void RefreshEngineStatus();
        void StatusTimer_Tick(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_statusTimer{ nullptr };
    };
}

namespace winrt::winui::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}