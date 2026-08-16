#pragma once

#include "BlockLogPage.g.h"

namespace winrt::winui::implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage>
    {
        BlockLogPage();

        void Refresh_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Clear_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void Load();
        void Timer_Tick(winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&);

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        uint64_t m_lastWrite{ 0 };
    };
}

namespace winrt::winui::factory_implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage, implementation::BlockLogPage>
    {
    };
}