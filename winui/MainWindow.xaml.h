#pragma once

#include "MainWindow.g.h"

namespace winrt::winui::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void NavView_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        void NavigateToTag(winrt::hstring const& tag);

        void NavigateFrameToTag(winrt::hstring const& tag);
        winrt::hstring m_currentTag{ L"Home" };
    };
}

namespace winrt::winui::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}