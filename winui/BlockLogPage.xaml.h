#pragma once

#include "BlockLogPage.g.h"
#include "LabelStorage.h"
#include <map>
#include <string>
#include <vector>

namespace winrt::winui::implementation
{
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

    private:
        void Load();
        void ReloadFromFile();
        void ApplyFilter();

        void Timer_Tick(winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&);
        void AddRuleFromSelection(bool whitelist);

        std::vector<std::wstring> m_allLines;
        std::vector<std::wstring> m_rawLines;
        std::wstring m_selectedRaw;

        std::map<std::wstring, SampleLabels::Sample> m_labels;
        std::wstring m_selectedDisplayText;

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        uint64_t m_lastWrite{ 0 };
    };
}

namespace winrt::winui::factory_implementation
{
    struct BlockLogPage : BlockLogPageT<BlockLogPage, implementation::BlockLogPage>
    {
    };
}