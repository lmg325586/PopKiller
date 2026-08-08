#include "pch.h"
#include "HomePage.xaml.h"
#include <winrt/Windows.System.Profile.h>
#include <string>
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
}

namespace winrt::winui::implementation
{
    HomePage::HomePage()
    {
        InitializeComponent();


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
}