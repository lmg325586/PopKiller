#include "pch.h"
#include "RuleIOPage.xaml.h"
#include "PopupBlocker.h"
#include "RuleStorage.h"
#include "App.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <algorithm>
#if __has_include("RuleIOPage.g.cpp")
#include "RuleIOPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    HWND GetAppHwnd()
    {
        auto window = winrt::winui::implementation::App::window;
        if (!window) return nullptr;
        auto native = window.try_as<::IWindowNative>();
        HWND hwnd{};
        if (native && SUCCEEDED(native->get_WindowHandle(&hwnd))) return hwnd;
        return nullptr;
    }

    std::wstring PickJsonFile(bool save)
    {
        wchar_t szFile[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetAppHwnd();
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"JSON 文件\0*.json\0所有文件\0*.*\0";
        ofn.lpstrDefExt = L"json";
        ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR)
            : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
        if (save ? ::GetSaveFileNameW(&ofn) : ::GetOpenFileNameW(&ofn))
            return szFile;
        return {};
    }
}

namespace winrt::winui::implementation
{
    void RuleIOPage::BackButton_Click(winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (Frame().CanGoBack()) Frame().GoBack();
    }

    void RuleIOPage::ExportButton_Click(winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        std::wstring path = PickJsonFile(true);
        if (path.empty()) return;

        auto commRef = ExportCommunityRules().IsChecked();
        bool includeCommunity = commRef ? commRef.GetBoolean() : false;

        auto tombRef = ExportTombstones().IsChecked();
        bool includeTombstones = tombRef ? tombRef.GetBoolean() : false;

        std::vector<PopupBlocker::Rule> rules;
        std::vector<std::wstring> removed;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            rules = PopupBlocker::Rules;
            removed = PopupBlocker::CommunityRemoved;
        }

        std::vector<PopupBlocker::Rule> filteredRules;
        for (auto const& r : rules)
        {
            if (r.fromCommunity && !includeCommunity) continue;
            filteredRules.push_back(r);
        }

        std::vector<std::wstring> filteredRemoved = includeTombstones ? removed : std::vector<std::wstring>{};

        bool ok = PopupBlocker::WriteUtf8StringToFile(path,
            PopupBlocker::SerializeRules(filteredRules, filteredRemoved));

        if (ok)
        {
            std::wstring msg = L"已导出 " + std::to_wstring(filteredRules.size()) + L" 条规则";
            if (includeTombstones)
                msg += L" 和 " + std::to_wstring(filteredRemoved.size()) + L" 条墓碑记录";
            msg += L"：" + path;
            ResultText().Text(msg);
        }
        else
        {
            ResultText().Text(L"导出失败：" + path);
        }
    }

    void RuleIOPage::ImportButton_Click(winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        std::wstring path = PickJsonFile(false);
        if (path.empty()) return;

        std::string text;
        if (!PopupBlocker::ReadFileToUtf8String(path, text))
        {
            ResultText().Text(L"导入失败：无法读取文件。");
            return;
        }

        std::vector<PopupBlocker::Rule> imported;
        if (!PopupBlocker::ParseRulesFromJsonString(text, imported))
        {
            ResultText().Text(L"导入失败：不是有效的规则 JSON 文件。");
            return;
        }

        std::vector<PopupBlocker::Rule> merged;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            merged = PopupBlocker::Rules;
        }

        size_t added = 0, skipped = 0;
        for (auto r : imported)
        {
            r.fromCommunity = false;
            std::wstring k = PopupBlocker::RuleKey(r);
            bool exists = std::any_of(merged.begin(), merged.end(),
                [&k](PopupBlocker::Rule const& e) { return PopupBlocker::RuleKey(e) == k; });
            if (exists) { ++skipped; continue; }
            merged.push_back(r);
            ++added;
        }

        if (added > 0) PopupBlocker::SaveRules(merged);
        ResultText().Text(L"导入完成：新增 " + std::to_wstring(added) +
            L" 条，跳过重复 " + std::to_wstring(skipped) + L" 条。");
    }
}