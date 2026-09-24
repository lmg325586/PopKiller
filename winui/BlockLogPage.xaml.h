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
        std::wstring exe;
        std::wstring title;
        std::wstring cls;        // 类名（搜索过滤用）
        std::wstring action;     // block/allow/monitor/kill
        std::wstring ev;         // SHOW/FG
        std::wstring reason;     // blacklist/whitelist/heuristic/...
        int count = 0;
        std::wstring firstTime;  // HH:MM:SS
        std::wstring lastTime;   // HH:MM:SS
        std::wstring lastRaw;    // 最后一条原始日志（右键菜单/标注用）
        bool mlY = false;        // ml=Y
        int score = 0;           // 启发式分数
        std::vector<std::wstring> raws;   // 该组全部原始日志（时间正序）
        bool expanded = false;            // 展开状态
    };

    struct UiRow { size_t groupIdx; long long rawIdx; };

    struct BlockLogPage : BlockLogPageT<BlockLogPage>
    {
        BlockLogPage();
        ~BlockLogPage();

        void Refresh_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Clear_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LogItem_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        void MarkPopup_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MarkNotPopup_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ExportSamples_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddToBlacklist_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddToWhitelist_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Filter_Changed(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void Search_Changed(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);

        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnNavigatedFrom(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void OnLogListLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void NewLogJumpButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        void LogItem_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);//展开合并

    private:

        void Load();
        void ReloadFromFile();
        void ApplyFilter();

        void Timer_Tick(winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&);
        void AddRuleFromSelection(bool whitelist);

        // 聚合与增量
        void AppendNewLines();
        std::wstring ExtractKey(std::wstring const& raw, LogGroup& g);
        std::wstring BuildGroupDisplay(LogGroup const& g);
        bool GroupPassFilter(LogGroup const& g, std::wstring const& filterTag,
            std::wstring const& searchText, int threshold);   // 4 参数，与 .cpp 对齐
        std::wstring CurrentFilterTag();
        std::wstring CurrentSearchText();
        void SyncJumpButton();
        void UpdateCountText();

        std::vector<LogGroup> m_groups;
        std::unordered_map<std::wstring, size_t> m_groupIndex; // key -> m_groups 下标
        std::vector<size_t> m_visibleGroups;                   // 当前可见的 group 下标（UI 顺序）

        std::wstring BuildRawDisplay(std::wstring const& raw);
        long long FindGroupRowIndex(size_t groupIdx);
        void ToggleExpand(size_t uiIdx);
        std::vector<UiRow> m_uiRows;

        std::map<std::wstring, SampleLabels::Sample> m_labels;
        std::wstring m_selectedRaw;

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        uint64_t m_lastWrite{ 0 };

        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_logScrollViewer{ nullptr };
        bool m_pinnedToTop{ true };
        bool m_inApplyFilter{ false };
        uint64_t m_lastFileSize{ 0 };
        uint32_t m_pendingNewCount{ 0 };
    };
}

namespace winrt::winui::factory_implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage, implementation::BlockLogPage>
    {
    };
}