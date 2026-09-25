#pragma once

#include "App.xaml.g.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <string>

namespace winrt::winui::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        // Toast 在应用运行中再次被点击时，由此事件回调送达
        void OnNotificationInvoked(
            winrt::Microsoft::Windows::AppNotifications::AppNotificationManager const& sender,
            winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs const& args);

        winrt::Microsoft::Windows::AppLifecycle::AppInstance m_keyInstance{ nullptr };

        inline static Microsoft::UI::Xaml::Window window{ nullptr };

    private:
        // 统一处理通知激活：解析 action/exe 并导航/加白
        void HandleNotification(
            winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs const& args);
    };
}