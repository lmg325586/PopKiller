#include "pch.h"
#include "HomePage.xaml.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.Profile.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Input.h>
#include <string>
#include <algorithm>
#include <cmath>
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Input;

namespace
{
    std::wstring ReadRegString(HKEY key, LPCWSTR value)
    {
        WCHAR buffer[512]{};
        DWORD size = sizeof(buffer);
        if (::RegGetValueW(key, nullptr, value, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
        {
            return buffer;
        }
        return {};
    }

    std::wstring EditionName(std::wstring const& id)
    {
        if (id == L"ProfessionalWorkstation")  return L"专业工作站版";
        if (id == L"Professional")             return L"专业版";
        if (id == L"Core")                     return L"家庭版";
        if (id == L"CoreSingleLanguage")       return L"家庭单语言版";
        if (id == L"Enterprise")               return L"企业版";
        if (id == L"Education")                return L"教育版";
        return id;
    }

    void PlaceEllipse(Shapes::Ellipse const& el, double x, double y)
    {
        double w = el.Width();
        double h = el.Height();
        el.SetValue(Controls::Canvas::LeftProperty(), box_value(x - w / 2));
        el.SetValue(Controls::Canvas::TopProperty(), box_value(y - h / 2));
    }

    void ClipToSelf(winrt::Microsoft::UI::Xaml::FrameworkElement const& el)
    {
        Media::RectangleGeometry clip;
        clip.Rect(Windows::Foundation::Rect{
            0, 0,
            static_cast<float>(el.ActualWidth()),
            static_cast<float>(el.ActualHeight()) });
        el.Clip(clip);
    }

    void UpdateGlow(FrameworkElement const& card, UIElement const& canvas,
        std::initializer_list<Shapes::Ellipse> layers,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        auto pos = e.GetCurrentPoint(card).Position();
        double w = card.ActualWidth();
        double h = card.ActualHeight();
        const double R = 100.0;

        double dx = pos.X < 0 ? -pos.X : (pos.X > w ? pos.X - w : 0.0);
        double dy = pos.Y < 0 ? -pos.Y : (pos.Y > h ? pos.Y - h : 0.0);
        double dist = std::sqrt(dx * dx + dy * dy);

        if (dist >= R)
        {
            canvas.Opacity(0.0);
            return;
        }

        canvas.Opacity(1.0 - dist / R);

        double cx = std::clamp(static_cast<double>(pos.X), 0.0, w);
        double cy = std::clamp(static_cast<double>(pos.Y), 0.0, h);
        for (auto const& el : layers) PlaceEllipse(el, cx, cy);
    }
}

namespace winrt::winui::implementation
{
    HomePage::HomePage()
    {
        InitializeComponent();

        GlowCard().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&)
            {
                ClipToSelf(GlowCanvas());
            });

        GlowCard2().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&)
            {
                ClipToSelf(GlowCanvas2());
            });

        uint64_t v = std::stoull(winrt::to_string(
            winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamilyVersion()));
        uint16_t build = static_cast<uint16_t>((v & 0x00000000FFFF0000) >> 16);
        uint16_t rev = static_cast<uint16_t>(v & 0x00000000FFFF);

        std::wstring displayVersion, editionId, owner, org;
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            displayVersion = ReadRegString(key, L"DisplayVersion");
            editionId = ReadRegString(key, L"EditionID");
            owner = ReadRegString(key, L"RegisteredOwner");
            org = ReadRegString(key, L"RegisteredOrganization");
            ::RegCloseKey(key);
        }

        std::wstring versionLine = L"版本 " + displayVersion +
            L" (OS 内部版本 " + std::to_wstring(build) + L"." + std::to_wstring(rev) + L")";
        VersionLineText().Text(hstring(versionLine));

        std::wstring baseName = (build >= 22000) ? L"Windows 11" : L"Windows 10";
        std::wstring edition = baseName + L" " + EditionName(editionId) +
            L" 操作系统及其用户界面受美国和其他国家/地区的商标法"
            L"和其他待颁布或已颁布的知识产权法保护。";
        EditionText().Text(hstring(edition));

        OwnerText().Text(hstring(owner));
        OrgText().Text(hstring(org));
    }

    void HomePage::RootPointerMoved(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        UpdateGlow(GlowCard(), GlowCanvas(),
            { GlowLayer5(), GlowLayer4(), GlowLayer3(), GlowLayer2(), GlowLayer1() }, e);
        UpdateGlow(GlowCard2(), GlowCanvas2(),
            { Glow2Layer5(), Glow2Layer4(), Glow2Layer3(), Glow2Layer2(), Glow2Layer1() }, e);
    }

    void HomePage::RootPointerExited(IInspectable const&, PointerRoutedEventArgs const&)
    {
        GlowCanvas().Opacity(0.0);
        GlowCanvas2().Opacity(0.0);
    }

    void HomePage::GoToBlocker_Tapped(IInspectable const&, TappedRoutedEventArgs const&)
    {
        auto window = winrt::winui::implementation::App::window;
        if (auto mainWindow = window.try_as<winrt::winui::MainWindow>())
        {
            mainWindow.NavigateToTag(L"Blocker");
        }
    }
}