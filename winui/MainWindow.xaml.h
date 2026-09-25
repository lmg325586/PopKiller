#pragma once

#include "MainWindow.g.h"
#include <functional>
#include "OwnerFunction.h"   // owner_function：携带注册者指针的回调槽（全局唯一定义）

namespace winrt::winui::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // 常驻（App/MainWindow 层）回调入口对应的自由函数，定义于 MainWindow.xaml.cpp。
        // 以"函数指针 + owner(MainWindow*)"经 owner_bind 存入全局回调槽，
        // 使 PopupBlockerPage 析构时能通过 owner() 精确识别并只清除自己注册的回调。
        void PersistentEnabledChanged(void* ctx);
        void PersistentCommunityRulesFetched(void* ctx);
        // 退出清理：先置 ShuttingDown，再持 CallbackMutex 摘除全部全局回调。
        void ClearAllCallbacksForShutdown();

        void NavView_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        void NavigateToTag(winrt::hstring const& tag);

        void NavigateFrameToTag(winrt::hstring const& tag);
        void HandleCloseRequested(
            winrt::Microsoft::UI::Windowing::AppWindow const& sender,
            winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);
        winrt::fire_and_forget ShowCloseConfirmation();
        void ExitApplication();

        // 常驻回调入口（App/MainWindow 层，生命周期与主窗口一致，不随设置页销毁而丢失）：
        // 托盘切换开关 / 社区规则后台更新时，即使 PopupBlockerPage 已析构也会执行，
        // 保证引擎状态与设置始终一致，重新进入页面时 UI 不会出现状态漂移。
        void OnEnabledChangedPersistent();
        void OnCommunityRulesFetched();
        // UI 线程：若当前正停留在 PopupBlockerPage，通知其从设置/引擎重新同步
        void RefreshVisibleBlockerPage();

        winrt::hstring m_currentTag{ L"Home" };
        bool m_forceClose{ false };
        bool m_closeDialogOpen{ false };
    };
}

// App/MainWindow 层常驻回调的自由函数（定义在 MainWindow.xaml.cpp，
// 位于 winrt::winui::implementation 命名空间）。
// 以"自由函数指针 + owner(MainWindow*)"形式经 owner_bind 存入回调槽，
// 使 PopupBlockerPage 析构时能通过 owner() 精确识别并只清除自己注册的回调。

// App/MainWindow 层常驻回调的自由函数（定义在 MainWindow.xaml.cpp，
// 位于 winrt::winui::implementation 命名空间）。
// 以"自由函数指针 + owner(MainWindow*)"形式经 owner_bind 存入回调槽，
// 使 PopupBlockerPage 析构时能通过 owner() 精确识别并只清除自己注册的回调。

namespace winrt::winui::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
