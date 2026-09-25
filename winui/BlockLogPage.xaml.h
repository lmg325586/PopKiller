#pragma once

#include "BlockLogPage.g.h"
#include "LabelStorage.h"
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

namespace winrt::winui::implementation
{
    struct LogGroup {
        std::wstring key;
        std::wstring exe, title, cls;
        std::wstring action, ev, reason;
        int count = 0;
        std::wstring firstTime, lastTime;
        std::wstring lastRaw;
        std::vector<std::wstring> raws;
        bool expanded = false;
        bool mlY = false;
        int score = 0;
        uint64_t seq = 0;
    };

    struct RowUi {
        winrt::Microsoft::UI::Xaml::Controls::Button chevronBtn{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border root{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel body{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::FontIcon chevron{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::RotateTransform rot{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border chip{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock actionText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border badgeBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock badgeText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock exeText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock titleText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock timeText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock reasonText{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::FontIcon labelIcon{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel subPanel{ nullptr };
    };

    struct BlockLogPage : BlockLogPageT<BlockLogPage>
    {
        BlockLogPage();
        ~BlockLogPage();

        void Refresh_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Clear_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MarkPopup_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MarkNotPopup_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ExportSamples_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddToBlacklist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddToWhitelist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Filter_Changed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void Search_Changed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);
        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnLogListLoaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void NewLogJumpButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RowChevronClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void Load();
        void ReloadFromFile();
        void ApplyFilter();
        void Timer_Tick(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void AddRuleFromSelection(bool whitelist);

        void AppendNewLines();
        std::wstring ExtractKey(std::wstring const& raw, LogGroup& g);
        bool GroupPassFilter(LogGroup const& g, std::wstring const& filterTag, std::wstring const& searchText, int threshold);
        std::wstring CurrentFilterTag();
        std::wstring CurrentSearchText();
        void SyncJumpButton();
        void UpdateCountText();

        RowUi BuildRow(size_t gidx);
        void UpdateRowUi(RowUi& ui, LogGroup const& g);
        winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildSubRow(std::wstring const& raw);
        void ToggleExpand(size_t gidx);
        void SetLabelIcon(winrt::Microsoft::UI::Xaml::Controls::FontIcon const& icon, std::wstring const& raw);
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout BuildMenu(winrt::Windows::Foundation::IInspectable const& tagValue);
        std::wstring ResolveRawFromTag(winrt::Windows::Foundation::IInspectable const& tag);

        std::vector<LogGroup> m_groups;
        std::unordered_map<std::wstring, size_t> m_groupIndex;
        std::vector<size_t> m_visibleGroups;
        std::vector<RowUi> m_rows;

        std::map<std::wstring, SampleLabels::Sample> m_labels;
        std::wstring m_selectedRaw;

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        uint64_t m_lastWrite{ 0 };
        uint64_t m_seq = 0;

        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_logScrollViewer{ nullptr };
        bool m_pinnedToTop{ true };
        bool m_inApplyFilter{ false };
        uint64_t m_lastFileSize{ 0 };
        uint32_t m_pendingNewCount{ 0 };
    };
}

namespace winrt::winui::factory_implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage, implementation::BlockLogPage> {};
}