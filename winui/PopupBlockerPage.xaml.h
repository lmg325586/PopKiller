#pragma once

#include "PopupBlockerPage.g.h"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "PopupBlocker.h"

namespace winrt::winui::implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage>
    {
        PopupBlockerPage();
        ~PopupBlockerPage();

        void EnableToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DeleteRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Pick_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchInput_TextChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void CommunityRulesToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RetryFetchButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EditRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OpenIO_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddCondition_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_statusTimer{ nullptr };
        void StatusTimer_Tick(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);
        winrt::Microsoft::UI::Xaml::Controls::Button m_resumeButton{ nullptr };
        void RefreshStatus();
        void ResumeButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void RuleItem_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        void RuleItem_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

        void RestoreCommunity_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void UpdateCommunityRestoreButtonVisibility();

    private:
        void UpdateCommunityStatus(bool ok, std::wstring const& msg);
        void RefreshList();
        void ReloadRulesFromEngine();
        void SelectRuleByRealIndex(size_t real);

        struct ConditionItem {
            int fieldType{ 0 };      // 0 exe,1 path,2 title,3 class,4 类名随机
            int matchMode{ 0 };      // 0 contains,1 exact,2 wildcard；fieldType==4 时忽略
            std::wstring pattern;    // fieldType==4 时为空
        };

        struct RuleItem {
            int listType{ 0 };
            std::vector<ConditionItem> conditions;  // >=1
            bool fromCommunity{ false };
        };

        struct ConditionRow {
            winrt::Microsoft::UI::Xaml::Controls::ComboBox fieldCombo{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::ComboBox modeCombo{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBox patternBox{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Button removeBtn{ nullptr };
        };
        std::vector<std::unique_ptr<ConditionRow>> m_addRows, m_editRows;
        bool m_populating{ false };

        // “编辑规则”对话框改为代码创建（避免声明在页面 XAML 树里导致的入场动画缺失，WinUI #8476 §9）
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_editListType{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_editConditionsPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_editAddButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_editCommunityNote{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog CreateEditDialog();

        void AddConditionRow(bool editArea, ConditionItem const& init);
        void RemoveConditionRow(bool editArea, ConditionRow* row);
        void SyncConditionRowEnabled(ConditionRow const& row);
        std::vector<ConditionItem> ReadConditions(bool editArea);
        void PopulateConditions(bool editArea, std::vector<ConditionItem> const& conds);
        void UpdateAddConditionButtons();
        std::wstring ConditionLabel(ConditionItem const& c) const;
        std::wstring RuleDisplay(RuleItem const& r) const;

        PopupBlocker::Rule ToEngineRule(RuleItem const& it);

        winrt::fire_and_forget OpenEditDialog(size_t real);
        winrt::fire_and_forget PromptConflictEdit(size_t real);

        bool m_initialized{ false };


        std::vector<RuleItem> m_rules;
        std::wstring m_searchText;
        std::vector<size_t> m_visibleIndex;

        size_t m_rightClickRealIndex{ (size_t)-1 };

    };
}

namespace winrt::winui::factory_implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage, implementation::PopupBlockerPage>
    {
    };
}