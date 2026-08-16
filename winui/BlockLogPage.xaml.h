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
    };
}

namespace winrt::winui::factory_implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage, implementation::BlockLogPage>
    {
    };
}