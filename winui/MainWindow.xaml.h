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
        void HandleCloseRequested(
            winrt::Microsoft::UI::Windowing::AppWindow const& sender,
            winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);
        winrt::fire_and_forget ShowCloseConfirmation();
        void ExitApplication();

        // 退出链路口：最早置位 ShuttingDown 并 join 引擎线程（幂等）
        void BeginShutdown();

        winrt::hstring m_currentTag{ L"Home" };
        bool m_forceClose{ false };
        bool m_closeDialogOpen{ false };
    };
}

namespace winrt::winui::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
