#include "pch.h"
#include "HomePage.xaml.h"
#include <winrt/Windows.System.Profile.h>
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::winui::implementation
{

    HomePage::HomePage()
    {
        InitializeComponent();

        uint64_t v = std::stoull(winrt::to_string(
            winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamilyVersion()));

        uint16_t major = static_cast<uint16_t>((v & 0xFFFF000000000000) >> 48);
        uint16_t minor = static_cast<uint16_t>((v & 0x0000FFFF00000000) >> 32);
        uint16_t build = static_cast<uint16_t>((v & 0x00000000FFFF0000) >> 16);
        uint16_t rev = static_cast<uint16_t>(v & 0x000000000000FFFF);

        hstring name = (build >= 22000) ? L"Windows 11" : L"Windows 10";

        WindowsVersionText().Text(
            name + L"  " + to_hstring(major) + L"." + to_hstring(minor) +
            L"  Build " + to_hstring(build) + L"." + to_hstring(rev));
    }

}
