#include "pch.h"
#include "HomePage.xaml.h"
#include <winrt/Windows.System.Profile.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Input.h>
#include <string>
#include <algorithm>
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

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


    void MakeEdgeBrush(Shapes::Shape const& edge, bool horizontal)
    {
        Media::LinearGradientBrush brush;
        brush.StartPoint(horizontal ? Windows::Foundation::Point{ 0.0f, 0.5f }
        : Windows::Foundation::Point{ 0.5f, 0.0f });
        brush.EndPoint(horizontal ? Windows::Foundation::Point{ 1.0f, 0.5f }
        : Windows::Foundation::Point{ 0.5f, 1.0f });

        for (int i = 0; i < 3; ++i)
        {
            Media::GradientStop stop;
            stop.Color(Windows::UI::Color{ 0x00, 0xD9, 0xD9, 0xD9 });
            stop.Offset(i == 0 ? 0.0 : (i == 1 ? 0.5 : 1.0));
            brush.GradientStops().Append(stop);
        }
        edge.Fill(brush);
    }

    void SetEdgeGlow(Shapes::Shape const& edge, double center, double spread, double factor)
    {
        auto brush = edge.Fill().try_as<Media::LinearGradientBrush>();
        if (!brush) return;

        auto stops = brush.GradientStops();
        stops.GetAt(0).Offset((std::max)(0.0, center - spread));
        stops.GetAt(1).Offset(std::clamp(center, 0.0, 1.0));
        stops.GetAt(2).Offset((std::min)(1.0, center + spread));

        uint8_t a = static_cast<uint8_t>(0xCC * std::clamp(factor, 0.0, 1.0));
        stops.GetAt(1).Color(Windows::UI::Color{ a, 0xD9, 0xD9, 0xD9 });
    }
}

namespace winrt::winui::implementation
{
    HomePage::HomePage()
    {
        InitializeComponent();

        MakeEdgeBrush(EdgeTop(), true);
        MakeEdgeBrush(EdgeBottom(), true);
        MakeEdgeBrush(EdgeLeft(), false);
        MakeEdgeBrush(EdgeRight(), false);

        uint64_t v = std::stoull(winrt::to_string(
            winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamilyVersion()));
        uint16_t build = static_cast<uint16_t>((v & 0x00000000FFFF0000) >> 16);
        uint16_t rev = static_cast<uint16_t>(v & 0x000000000000FFFF);

        std::wstring displayVersion, productName, owner, org;
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            displayVersion = ReadRegString(key, L"DisplayVersion");
            productName = ReadRegString(key, L"ProductName");
            owner = ReadRegString(key, L"RegisteredOwner");
            org = ReadRegString(key, L"RegisteredOrganization");
            ::RegCloseKey(key);
        }

        std::wstring versionLine = L"版本 " + displayVersion +
            L" (OS 内部版本 " + std::to_wstring(build) + L"." + std::to_wstring(rev) + L")";
        VersionLineText().Text(hstring(versionLine));

        std::wstring edition = productName +
            L" 操作系统及其用户界面受美国和其他国家/地区的商标法"
            L"和其他待颁布或已颁布的知识产权法保护。";
        EditionText().Text(hstring(edition));

        OwnerText().Text(hstring(owner));
        OrgText().Text(hstring(org));
    }

    void HomePage::GlowPointerEntered(IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        GlowRing().Opacity(1.0);
    }

    void HomePage::GlowPointerMoved(IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        double w = GlowCard().ActualWidth();
        double h = GlowCard().ActualHeight();
        if (w <= 0 || h <= 0) return;

        auto pos = e.GetCurrentPoint(GlowCard()).Position();
        double tx = std::clamp(pos.X / w, 0.0, 1.0);
        double ty = std::clamp(pos.Y / h, 0.0, 1.0);

        const double spread = 0.25;


        SetEdgeGlow(EdgeTop(), tx, spread, 1.0 - ty);
        SetEdgeGlow(EdgeBottom(), tx, spread, ty);
        SetEdgeGlow(EdgeLeft(), ty, spread, 1.0 - tx);
        SetEdgeGlow(EdgeRight(), ty, spread, tx);
    }

    void HomePage::GlowPointerExited(IInspectable const&,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        GlowRing().Opacity(0.0);
    }
}