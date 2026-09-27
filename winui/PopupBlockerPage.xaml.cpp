#include "pch.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "PopupBlocker.h"
#include "RuleStorage.h" 
#include "WindowPicker.h"
#include "App.xaml.h"
#include "RuleIOPage.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <sstream>
#if __has_include("PopupBlockerPage.g.cpp")
#include "PopupBlockerPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    // 单条规则最多允许的条件数
    inline constexpr size_t kMaxConditions = 4;

    const wchar_t* ListTypeKey(int idx) { return idx == 1 ? L"W" : L"B"; }
    const wchar_t* ListTypeLabel(int idx) { return idx == 1 ? L"白名单" : L"黑名单"; }

    const wchar_t* FieldKey(int idx)
    {
        switch (idx) {
        case 0:  return L"exe";
        case 1:  return L"path";
        case 2:  return L"title";
        default: return L"class";
        }
    }

    const wchar_t* FieldLabel(int idx)
    {
        switch (idx) {
        case 0:  return L"进程";
        case 1:  return L"路径";
        case 2:  return L"标题";
        case 4:  return L"类名随机";
        default: return L"类名";
        }
    }

    const wchar_t* MatchModeKey(int idx)
    {
        switch (idx) {
        case 0:  return L"contains";
        case 1:  return L"exact";
        default: return L"wildcard";
        }
    }

    const wchar_t* MatchModeLabel(int idx)
    {
        switch (idx) {
        case 0:  return L"包含";
        case 1:  return L"精确";
        default: return L"通配符";
        }
    }
}

namespace winrt::winui::implementation
{

    PopupBlockerPage::~PopupBlockerPage()
    {
        if (m_statusTimer) m_statusTimer.Stop();
        // 加锁清空回调，防止引擎后台线程在页面析构瞬间调用
        std::lock_guard lock(PopupBlocker::CallbackMutex);
        PopupBlocker::EnabledChangedCallback = nullptr;
        PopupBlocker::CommunityRulesFetchCallback = nullptr;
    }

    PopupBlockerPage::PopupBlockerPage()
    {
        InitializeComponent();

        this->NavigationCacheMode(Navigation::NavigationCacheMode::Disabled);

        AddConditionRow(false, ConditionItem{});

        m_statusTimer = DispatcherTimer();
        m_statusTimer.Interval(std::chrono::milliseconds{ 500 });
        m_statusTimer.Tick({ get_weak(), &PopupBlockerPage::StatusTimer_Tick });
        m_statusTimer.Start();
        m_resumeButton = winrt::Microsoft::UI::Xaml::Controls::Button();
        m_resumeButton.Content(winrt::box_value(L"立即恢复"));
        m_resumeButton.Click({ get_weak(), &PopupBlockerPage::ResumeButton_Click });

        PopupBlocker::EnsureDefaultRules();

        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);

        // 外层与内层 Lambda 全部捕获弱引用
        auto setupCallbacks = [weakThis = get_weak()]()
            {
                if (auto self = weakThis.get())
                {
                    PopupBlocker::EnabledChangedCallback = [weakThis]()
                        {
                            if (auto s = weakThis.get()) {
                                s->m_initialized = false;
                                s->EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);
                                s->m_initialized = true;
                            }
                        };

                    PopupBlocker::CommunityRulesFetchCallback = [weakThis](bool ok, std::wstring msg)
                        {
                            if (auto s = weakThis.get()) {
                                s->DispatcherQueue().TryEnqueue([weakThis, ok, msg]()
                                    {
                                        if (auto s2 = weakThis.get()) s2->UpdateCommunityStatus(ok, msg);
                                    });
                            }
                        };
                }
            };

        this->Loaded([setupCallbacks](auto&&, auto&&) {
            std::lock_guard lock(PopupBlocker::CallbackMutex);
            setupCallbacks();
            });

        m_initialized = true;

        PopupBlocker::SyncFromSettings();
        ReloadRulesFromEngine();

        CommunityRulesToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"CommunityRulesEnabled", 1) == 1);
        if (CommunityRulesToggle().IsOn()) {
            CommunityStatusText().Text(L"正在拉取社区规则…");
            PopupBlocker::FetchCommunityRulesAsync();
        }
    }

    PopupBlocker::Rule PopupBlockerPage::ToEngineRule(RuleItem const& it)
    {
        PopupBlocker::Rule r;
        r.isWhitelist = (it.listType == 1);
        r.fromCommunity = it.fromCommunity;

        if (it.conditions.empty()) {
            r.conditions.push_back(PopupBlocker::RuleCondition{
                PopupBlocker::RuleField::Exe, PopupBlocker::MatchMode::Contains, L"" });
            return r;
        }

        for (auto const& c : it.conditions) {
            PopupBlocker::RuleCondition rc;
            if (c.fieldType == 4) {
                rc.field = PopupBlocker::RuleField::Class;
                rc.mode = PopupBlocker::MatchMode::RandomClass;
                rc.pattern.clear();
            }
            else {
                switch (c.fieldType) {
                case 0: rc.field = PopupBlocker::RuleField::Exe; break;
                case 1: rc.field = PopupBlocker::RuleField::Path; break;
                case 2: rc.field = PopupBlocker::RuleField::Title; break;
                default: rc.field = PopupBlocker::RuleField::Class; break;
                }
                switch (c.matchMode) {
                case 0: rc.mode = PopupBlocker::MatchMode::Contains; break;
                case 1: rc.mode = PopupBlocker::MatchMode::Exact; break;
                default: rc.mode = PopupBlocker::MatchMode::Wildcard; break;
                }
                rc.pattern = PopupBlocker::Lower(c.pattern);
            }
            r.conditions.push_back(std::move(rc));
        }
        return r;
    }

    std::wstring PopupBlockerPage::ConditionLabel(ConditionItem const& c) const
    {
        if (c.fieldType == 4) return L"类名随机";
        return std::wstring(FieldLabel(c.fieldType)) + L"|" +
            MatchModeLabel(c.matchMode) + L":" + c.pattern;
    }

    std::wstring PopupBlockerPage::RuleDisplay(RuleItem const& r) const
    {
        std::wstring display = (r.fromCommunity ? L"[社区] " : L"") +
            std::wstring(ListTypeLabel(r.listType)) + L" | ";
        for (size_t i = 0; i < r.conditions.size(); ++i) {
            if (i) display += L" + ";
            display += ConditionLabel(r.conditions[i]);
        }
        return display;
    }

    void PopupBlockerPage::SyncConditionRowEnabled(ConditionRow const& row)
    {
        bool randomClass = row.fieldCombo.SelectedIndex() == 4;
        row.modeCombo.Visibility(randomClass ? Visibility::Collapsed : Visibility::Visible);
        row.patternBox.Visibility(randomClass ? Visibility::Collapsed : Visibility::Visible);
    }

    void PopupBlockerPage::AddConditionRow(bool editArea, ConditionItem const& init)
    {
        auto row = std::make_unique<ConditionRow>();

        row->fieldCombo = Controls::ComboBox();
        row->fieldCombo.MinWidth(96);
        row->fieldCombo.Items().Append(box_value(hstring(L"进程")));
        row->fieldCombo.Items().Append(box_value(hstring(L"路径")));
        row->fieldCombo.Items().Append(box_value(hstring(L"标题")));
        row->fieldCombo.Items().Append(box_value(hstring(L"类名")));
        row->fieldCombo.Items().Append(box_value(hstring(L"类名随机")));

        row->modeCombo = Controls::ComboBox();
        row->modeCombo.MinWidth(80);
        row->modeCombo.Items().Append(box_value(hstring(L"包含")));
        row->modeCombo.Items().Append(box_value(hstring(L"精确")));
        row->modeCombo.Items().Append(box_value(hstring(L"通配符")));

        row->patternBox = Controls::TextBox();
        row->patternBox.PlaceholderText(L"如 *pop* 或完整路径");
        row->patternBox.HorizontalAlignment(HorizontalAlignment::Stretch);

        row->removeBtn = Controls::Button();
        row->removeBtn.Content(box_value(hstring(L"✕")));
        row->removeBtn.Padding(ThicknessHelper::FromLengths(8, 4, 8, 4));

        auto grid = Controls::Grid();
        grid.ColumnSpacing(6);
        {
            Controls::ColumnDefinition c0; c0.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
            Controls::ColumnDefinition c1; c1.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
            Controls::ColumnDefinition c2; c2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            Controls::ColumnDefinition c3; c3.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
            grid.ColumnDefinitions().Append(c0);
            grid.ColumnDefinitions().Append(c1);
            grid.ColumnDefinitions().Append(c2);
            grid.ColumnDefinitions().Append(c3);
        }
        Controls::Grid::SetColumn(row->fieldCombo, 0);
        Controls::Grid::SetColumn(row->modeCombo, 1);
        Controls::Grid::SetColumn(row->patternBox, 2);
        Controls::Grid::SetColumn(row->removeBtn, 3);
        grid.Children().Append(row->fieldCombo);
        grid.Children().Append(row->modeCombo);
        grid.Children().Append(row->patternBox);
        grid.Children().Append(row->removeBtn);

        {
            bool prev = m_populating;
            m_populating = true;
            int f = (init.fieldType >= 0 && init.fieldType <= 4) ? init.fieldType : 0;
            row->fieldCombo.SelectedIndex(f);
            int m = (init.matchMode >= 0 && init.matchMode <= 2) ? init.matchMode : 0;
            row->modeCombo.SelectedIndex(m);
            row->patternBox.Text(hstring(init.pattern));
            m_populating = prev;
        }

        ConditionRow* rowPtr = row.get();
        row->fieldCombo.SelectionChanged(
            [weakThis = get_weak(), rowPtr](IInspectable const&, Controls::SelectionChangedEventArgs const&)
            {
                if (auto self = weakThis.get()) self->SyncConditionRowEnabled(*rowPtr);
            });
        row->removeBtn.Click(
            [weakThis = get_weak(), editArea, rowPtr](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto self = weakThis.get()) self->RemoveConditionRow(editArea, rowPtr);
            });

        SyncConditionRowEnabled(*row);

        (editArea ? EditConditionsPanel() : AddConditionsPanel()).Children().Append(grid);
        (editArea ? m_editRows : m_addRows).push_back(std::move(row));
        UpdateAddConditionButtons();
    }

    void PopupBlockerPage::RemoveConditionRow(bool editArea, ConditionRow* row)
    {
        auto& rows = editArea ? m_editRows : m_addRows;
        auto it = std::find_if(rows.begin(), rows.end(),
            [row](std::unique_ptr<ConditionRow> const& p) { return p.get() == row; });
        if (it == rows.end()) return;

        // 行与面板子元素顺序一一对应
        auto index = static_cast<uint32_t>(it - rows.begin());
        auto panel = editArea ? EditConditionsPanel() : AddConditionsPanel();
        if (index < panel.Children().Size()) panel.Children().RemoveAt(index);

        rows.erase(it);
        if (rows.empty()) AddConditionRow(editArea, ConditionItem{});
        UpdateAddConditionButtons();
    }

    std::vector<PopupBlockerPage::ConditionItem> PopupBlockerPage::ReadConditions(bool editArea)
    {
        std::vector<ConditionItem> out;
        auto& rows = editArea ? m_editRows : m_addRows;
        for (auto const& row : rows) {
            ConditionItem c;
            int f = row->fieldCombo.SelectedIndex();
            if (f < 0) f = 0;
            c.fieldType = f;
            if (f == 4) {
                c.matchMode = 0;
                c.pattern.clear();
            }
            else {
                int m = row->modeCombo.SelectedIndex();
                c.matchMode = (m < 0) ? 0 : m;
                c.pattern = std::wstring(row->patternBox.Text());
            }
            out.push_back(std::move(c));
        }
        return out;
    }

    void PopupBlockerPage::PopulateConditions(bool editArea, std::vector<ConditionItem> const& conds)
    {
        {
            bool prev = m_populating;
            m_populating = true;
            (editArea ? EditConditionsPanel() : AddConditionsPanel()).Children().Clear();
            (editArea ? m_editRows : m_addRows).clear();
            m_populating = prev;
        }

        for (auto const& c : conds) AddConditionRow(editArea, c);
        if ((editArea ? m_editRows : m_addRows).empty()) AddConditionRow(editArea, ConditionItem{});
        UpdateAddConditionButtons();
    }

    void PopupBlockerPage::UpdateAddConditionButtons()
    {
        if (AddConditionButton()) AddConditionButton().IsEnabled(m_addRows.size() < kMaxConditions);
        if (EditAddConditionButton()) EditAddConditionButton().IsEnabled(m_editRows.size() < kMaxConditions);

        // 只有一个条件时隐藏该行的删除按钮
        bool showAddRemove = m_addRows.size() > 1;
        for (auto& r : m_addRows) if (r->removeBtn) r->removeBtn.Visibility(showAddRemove ? Visibility::Visible : Visibility::Collapsed);
        bool showEditRemove = m_editRows.size() > 1;
        for (auto& r : m_editRows) if (r->removeBtn) r->removeBtn.Visibility(showEditRemove ? Visibility::Visible : Visibility::Collapsed);
    }

    void PopupBlockerPage::AddCondition_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_addRows.size() >= kMaxConditions) return;
        AddConditionRow(false, ConditionItem{});
    }

    void PopupBlockerPage::EditAddCondition_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_editRows.size() >= kMaxConditions) return;
        AddConditionRow(true, ConditionItem{});
    }

    void PopupBlockerPage::ReloadRulesFromEngine()
    {
        m_rules.clear();
        std::vector<PopupBlocker::Rule> cur;
        { std::lock_guard lock(PopupBlocker::RulesMutex); cur = PopupBlocker::Rules; }
        for (auto const& r : cur)
        {
            RuleItem item{};
            item.listType = r.isWhitelist ? 1 : 0;
            item.fromCommunity = r.fromCommunity;
            for (auto const& c : r.conditions) {
                ConditionItem ci;
                if (c.mode == PopupBlocker::MatchMode::RandomClass) {
                    ci.fieldType = 4;
                    ci.matchMode = 0;
                    ci.pattern.clear();
                }
                else {
                    ci.fieldType = static_cast<int>(c.field);
                    ci.matchMode = static_cast<int>(c.mode);
                    ci.pattern = c.pattern;
                }
                item.conditions.push_back(std::move(ci));
            }
            if (item.conditions.empty()) item.conditions.push_back(ConditionItem{});
            m_rules.push_back(std::move(item));
        }

        std::stable_partition(m_rules.begin(), m_rules.end(),
            [](RuleItem const& r) { return !r.fromCommunity; });

        RefreshList();
        UpdateCommunityRestoreButtonVisibility();
    }

    void PopupBlockerPage::RefreshList()
    {
        RulesList().Items().Clear();
        m_visibleIndex.clear();

        auto appendItem = [this](size_t i)
            {
                auto const& r = m_rules[i];
                std::wstring display = RuleDisplay(r);

                if (!m_searchText.empty() &&
                    PopupBlocker::Lower(display).find(m_searchText) == std::wstring::npos)
                    return;

                m_visibleIndex.push_back(i);
                RulesList().Items().Append(box_value(hstring(display)));
            };

        for (size_t i = 0; i < m_rules.size(); ++i)
            if (!m_rules[i].fromCommunity) appendItem(i);
        for (size_t i = 0; i < m_rules.size(); ++i)
            if (m_rules[i].fromCommunity) appendItem(i);

        UpdateCommunityRestoreButtonVisibility();
    }

    void PopupBlockerPage::SelectRuleByRealIndex(size_t real)
    {
        if (real >= m_rules.size()) return;

        // 若该规则被搜索过滤掉，先清空搜索以便显示
        if (!m_searchText.empty())
        {
            SearchInput().Text(L"");
            m_searchText.clear();
            RefreshList();
        }

        for (size_t v = 0; v < m_visibleIndex.size(); ++v)
        {
            if (m_visibleIndex[v] == real)
            {
                RulesList().SelectedIndex(static_cast<int>(v));
                if (auto item = RulesList().Items().GetAt(static_cast<uint32_t>(v)))
                    RulesList().ScrollIntoView(item);
                return;
            }
        }
    }

    winrt::fire_and_forget PopupBlockerPage::PromptConflictEdit(size_t real)
    {
        auto lifetime = get_strong();
        if (real >= m_rules.size()) co_return;

        auto xamlRoot = this->XamlRoot();
        if (!xamlRoot) co_return;

        auto const& r = m_rules[real];
        std::wstring display = RuleDisplay(r);
        ConflictText().Text(hstring(
            L"已存在相同内容的相反名单规则：\n" + display +
            L"\n\n是否打开该规则进行编辑？（白名单优先，不处理则该窗口将被放行。）"));

        ConflictDialog().XamlRoot(xamlRoot);
        auto result = co_await ConflictDialog().ShowAsync();
        if (result != Controls::ContentDialogResult::Primary) co_return;

        // 重置"添加规则"栏目
        PopulateConditions(false, std::vector<ConditionItem>{ ConditionItem{} });
        ListTypeCombo().SelectedIndex(0);
        PickInfo().Text(L"");
        PickInfo().Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));

        SelectRuleByRealIndex(real);
        OpenEditDialog(real);
    }

    void PopupBlockerPage::EnableToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        bool on = EnableToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"Enabled", on ? 1 : 0);
        if (on)
        {
            PopupBlocker::SyncFromSettings();
            HeuristicML::GetInstance().Init();
            PopupBlocker::Start();
            RefreshStatus();
        }
        else
        {
            PopupBlocker::Stop();
            RefreshStatus();
        }
    }

    void PopupBlockerPage::CommunityRulesToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        if (!m_initialized) return;
        bool on = CommunityRulesToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"CommunityRulesEnabled", on ? 1 : 0);

        if (on) {
            CommunityStatusText().Text(L"正在拉取社区规则…");
            RetryFetchButton().Visibility(Visibility::Collapsed);
            PopupBlocker::FetchCommunityRulesAsync();
        }
        else {
            CommunityStatusText().Text(L"");
            RetryFetchButton().Visibility(Visibility::Collapsed);

            PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>& rules, std::vector<std::wstring>&) {
                rules.erase(std::remove_if(rules.begin(), rules.end(),
                    [](PopupBlocker::Rule const& r) { return r.fromCommunity; }), rules.end());
            });
            ReloadRulesFromEngine();
        }
    }

    void PopupBlockerPage::UpdateCommunityStatus(bool ok, std::wstring const& msg)
    {
        auto self = get_strong();
        if (!self) return;

        if (!CommunityRulesToggle().IsOn()) {
            CommunityStatusText().Text(L"");
            RetryFetchButton().Visibility(Visibility::Collapsed);
            return;
        }
        if (ok) {
            CommunityStatusText().Text(L"社区规则已更新，新增 " + winrt::hstring(msg) + L" 条");
            CommunityStatusText().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
            RetryFetchButton().Visibility(Visibility::Collapsed);
            ReloadRulesFromEngine();
        }
        else {
            CommunityStatusText().Text(L"社区规则拉取失败：" + winrt::hstring(msg));
            CommunityStatusText().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
            RetryFetchButton().Visibility(Visibility::Visible);
        }
    }

    void PopupBlockerPage::RetryFetchButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        CommunityStatusText().Text(L"正在拉取社区规则…");
        RetryFetchButton().Visibility(Visibility::Collapsed);
        PopupBlocker::FetchCommunityRulesAsync();
    }

    void PopupBlockerPage::EditRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        size_t real = (size_t)-1;
        if (m_rightClickRealIndex != (size_t)-1 && m_rightClickRealIndex < m_rules.size())
        {
            real = m_rightClickRealIndex;
            m_rightClickRealIndex = (size_t)-1;
        }
        else
        {
            int idx = RulesList().SelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;
            real = m_visibleIndex[static_cast<size_t>(idx)];
        }

        OpenEditDialog(real);
    }

    void PopupBlockerPage::AddRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::vector<ConditionItem> conds = ReadConditions(false);
        if (conds.empty()) conds.push_back(ConditionItem{});

        for (auto const& c : conds) {
            if (c.fieldType != 4 && c.pattern.empty()) {
                PickInfo().Text(L"⚠ 模式串不能为空，未添加。");
                PickInfo().Foreground(Media::SolidColorBrush(
                    winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
                return;
            }
        }

        int listType = ListTypeCombo().SelectedIndex();

        // 规范化条件集合（忽略名单类型）后比较，实现 A+B 与 B+A 视为同一规则
        RuleItem normalized{};
        normalized.listType = 0;
        normalized.conditions = conds;
        std::wstring candKey = PopupBlocker::RuleKey(ToEngineRule(normalized));

        bool conflict = false;
        size_t conflictReal = (size_t)-1;
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            if (m_rules[i].listType == listType) continue;
            RuleItem norm{};
            norm.listType = 0;
            norm.conditions = m_rules[i].conditions;
            if (PopupBlocker::RuleKey(ToEngineRule(norm)) == candKey)
            {
                conflict = true;
                conflictReal = i;
                break;
            }
        }

        if (conflict)
        {
            // 有冲突：不添加新规则，弹确认框询问是否编辑冲突规则
            PickInfo().Text(L"⚠ 检测到冲突：已存在相同内容的相反名单规则。");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
            PromptConflictEdit(conflictReal);
            return;
        }

        RuleItem item{};
        item.listType = listType;
        item.conditions = conds;
        item.fromCommunity = false;
        PopupBlocker::Rule nr = ToEngineRule(item);
        bool added = false;
        PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>& rules, std::vector<std::wstring>&) {
            std::wstring k = PopupBlocker::RuleKey(nr);
            bool exists = std::any_of(rules.begin(), rules.end(),
                [&](PopupBlocker::Rule const& e) { return PopupBlocker::RuleKey(e) == k; });
            if (!exists) { rules.push_back(nr); added = true; }
        });

        PopulateConditions(false, std::vector<ConditionItem>{ ConditionItem{} });
        ReloadRulesFromEngine();

        if (!added) {
            PickInfo().Text(L"⚠ 相同规则已存在，未重复添加。");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
            return;
        }

        PickInfo().Text(L"");
        PickInfo().Foreground(Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
    }

    void PopupBlockerPage::DeleteRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        size_t real = (size_t)-1;
        if (m_rightClickRealIndex != (size_t)-1 && m_rightClickRealIndex < m_rules.size())
        {
            real = m_rightClickRealIndex;
            m_rightClickRealIndex = (size_t)-1;
        }
        else
        {
            int idx = RulesList().SelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;
            real = m_visibleIndex[static_cast<size_t>(idx)];
        }

        std::wstring key = PopupBlocker::RuleKey(ToEngineRule(m_rules[real]));
        bool fromCommunity = m_rules[real].fromCommunity;
        PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>& rules, std::vector<std::wstring>& removed) {
            if (fromCommunity) removed.push_back(key);
            rules.erase(std::remove_if(rules.begin(), rules.end(),
                [&](PopupBlocker::Rule const& r) { return PopupBlocker::RuleKey(r) == key; }), rules.end());
        });

        ReloadRulesFromEngine();
    }

    void PopupBlockerPage::Pick_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto window = winrt::winui::implementation::App::window;
        if (!window) return;
        auto native = window.try_as<::IWindowNative>();
        HWND hwnd{};
        if (!native || FAILED(native->get_WindowHandle(&hwnd)) || !hwnd) return;

        WindowPicker::Start(hwnd, [this](WindowPicker::PickResult r)
            {
                if (m_addRows.empty()) AddConditionRow(false, ConditionItem{});
                ConditionRow* row = m_addRows.back().get();
                int fieldIdx = row->fieldCombo.SelectedIndex();
                if (fieldIdx == 4 && m_addRows.size() < kMaxConditions) {
                    // 最后一行是"类名随机"，无法填入模式串，新增一行承载
                    AddConditionRow(false, ConditionItem{});
                    row = m_addRows.back().get();
                    fieldIdx = row->fieldCombo.SelectedIndex();
                }

                std::wstring value;
                switch (fieldIdx) {
                case 0:  value = r.exe;         break;
                case 1:  value = r.processPath; break;
                case 2:  value = r.title;       break;
                default: value = r.className;   break;
                }
                if (value.empty()) value = r.exe;

                row->patternBox.Text(hstring(value));
                PickInfo().Text(L"exe: " + r.exe +
                    L"\npath: " + r.processPath +
                    L"\nclass: " + r.className +
                    L"\ntitle: " + r.title);
                PickInfo().Foreground(Media::SolidColorBrush(
                    winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
            });
    }

    void PopupBlockerPage::SearchInput_TextChanged(IInspectable const&,
        Controls::TextChangedEventArgs const&)
    {
        m_searchText = PopupBlocker::Lower(std::wstring(SearchInput().Text()));
        RefreshList();
    }

    void PopupBlockerPage::OpenIO_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().Navigate(xaml_typename<winui::RuleIOPage>());
    }

    void PopupBlockerPage::StatusTimer_Tick(IInspectable const&, IInspectable const&)
    {
        if (PopupBlocker::ShuttingDown.load()) return;

        auto self = get_strong();
        if (!self) return;
        RefreshStatus();
    }

    void PopupBlockerPage::RefreshStatus()
    {
        auto self = get_strong();
        if (!self) return;

        bool enabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        bool paused = PopupBlocker::Paused.load();

        using namespace winrt::Microsoft::UI::Xaml::Controls;

        if (!enabled) {
            StatusInfoBar().Severity(InfoBarSeverity::Informational);
            StatusInfoBar().Title(L"拦截已关闭");
            StatusInfoBar().Message(L"当前不会拦截任何弹窗。");
            StatusInfoBar().ActionButton(nullptr);
        }
        else if (paused) {
            StatusInfoBar().Severity(InfoBarSeverity::Warning);
            StatusInfoBar().Title(L"拦截已暂停");

            long long deadline = PopupBlocker::PauseDeadlineMs.load();
            long long now = PopupBlocker::NowMs();
            long long remaining = deadline - now;
            if (remaining < 0) remaining = 0;

            int totalSeconds = static_cast<int>(remaining / 1000);
            int minutes = totalSeconds / 60;
            int seconds = totalSeconds % 60;

            std::wstring msg = L"将在 " + std::to_wstring(minutes) + L" 分 "
                + std::to_wstring(seconds) + L" 秒后自动恢复拦截。";
            StatusInfoBar().Message(winrt::hstring(msg));
            StatusInfoBar().ActionButton(m_resumeButton);
        }
        else {
            StatusInfoBar().Severity(InfoBarSeverity::Success);
            StatusInfoBar().Title(L"拦截运行中");
            StatusInfoBar().Message(L"正在实时监控并拦截弹窗。");
            StatusInfoBar().ActionButton(nullptr);
        }
    }

    void PopupBlockerPage::ResumeButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        PopupBlocker::ResumeNow();
        RefreshStatus();
    }

    void PopupBlockerPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        ReloadRulesFromEngine();
        RefreshList();
    }

    void PopupBlockerPage::RuleItem_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&)
    {
        m_rightClickRealIndex = (size_t)-1;
        if (auto tb = sender.try_as<Controls::TextBlock>())
        {
            uint32_t uiIdx = 0;
            if (RulesList().Items().IndexOf(box_value(tb.Text()), uiIdx)
                && uiIdx < m_visibleIndex.size())
            {
                m_rightClickRealIndex = m_visibleIndex[uiIdx];
                RulesList().SelectedIndex(uiIdx);
            }
        }
    }

    void PopupBlockerPage::RuleItem_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
    {
        if (auto tb = sender.try_as<Controls::TextBlock>())
        {
            uint32_t uiIdx = 0;
            if (RulesList().Items().IndexOf(box_value(tb.Text()), uiIdx) && uiIdx < m_visibleIndex.size())
                OpenEditDialog(m_visibleIndex[uiIdx]);
        }
    }

    winrt::fire_and_forget PopupBlockerPage::OpenEditDialog(size_t real)
    {
        auto lifetime = get_strong();
        if (real >= m_rules.size()) co_return;
        auto const& it = m_rules[real];

        auto xamlRoot = this->XamlRoot();
        if (!xamlRoot) co_return;

        EditDialog().XamlRoot(xamlRoot);
        EditListTypeCombo().SelectedIndex(it.listType);
        PopulateConditions(true, it.conditions);
        EditCommunityNote().Visibility(it.fromCommunity ? Visibility::Visible : Visibility::Collapsed);

        auto result = co_await EditDialog().ShowAsync();
        if (result != Controls::ContentDialogResult::Primary) co_return;

        std::vector<ConditionItem> conds = ReadConditions(true);
        if (conds.empty()) conds.push_back(ConditionItem{});

        for (auto const& c : conds)
        {
            if (c.fieldType != 4 && c.pattern.empty())
            {
                PickInfo().Text(L"⚠ 模式串不能为空，未保存。");
                PickInfo().Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
                co_return;
            }
        }

        int listType = EditListTypeCombo().SelectedIndex();

        RuleItem updatedItem{};
        updatedItem.listType = listType;
        updatedItem.conditions = conds;
        updatedItem.fromCommunity = false;

        // 规范化条件集合（忽略名单类型）后比较，实现 A+B 与 B+A 视为同一规则
        RuleItem normalized{};
        normalized.listType = 0;
        normalized.conditions = conds;
        std::wstring updKey = PopupBlocker::RuleKey(ToEngineRule(normalized));

        bool conflict = false;
        size_t conflictReal = (size_t)-1;
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            if (i == real) continue;
            auto const& r = m_rules[i];
            if (r.listType == listType) continue;
            RuleItem norm{};
            norm.listType = 0;
            norm.conditions = r.conditions;
            if (PopupBlocker::RuleKey(ToEngineRule(norm)) == updKey)
            {
                conflict = true; conflictReal = i; break;
            }
        }

        std::wstring oldKey = PopupBlocker::RuleKey(ToEngineRule(m_rules[real]));
        bool oldCommunity = m_rules[real].fromCommunity;
        std::wstring conflictKey;
        if (conflict) conflictKey = PopupBlocker::RuleKey(ToEngineRule(m_rules[conflictReal]));
        PopupBlocker::Rule updated = ToEngineRule(updatedItem);
        PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>& rules, std::vector<std::wstring>& removed) {
            if (oldCommunity) removed.push_back(oldKey);
            auto it = std::find_if(rules.begin(), rules.end(),
                [&](PopupBlocker::Rule const& r) { return PopupBlocker::RuleKey(r) == oldKey; });
            if (it != rules.end()) *it = updated;
            else rules.push_back(updated);
        });

        ReloadRulesFromEngine();

        if (conflict) {
            size_t conflictRealAfter = (size_t)-1;
            for (size_t i = 0; i < m_rules.size(); ++i)
                if (PopupBlocker::RuleKey(ToEngineRule(m_rules[i])) == conflictKey) { conflictRealAfter = i; break; }
            if (conflictRealAfter != (size_t)-1) SelectRuleByRealIndex(conflictRealAfter);
            PickInfo().Text(L"⚠ 已选中冲突规则：已存在相同内容的相反名单规则；白名单优先，该窗口将被放行。");
            PickInfo().Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
        }
        else {
            PickInfo().Text(L"");
        }
    }

    void PopupBlockerPage::RestoreCommunity_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool had = false;
        PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>&, std::vector<std::wstring>& removed) {
            had = !removed.empty();
            removed.clear();
        });

        if (had) {
            UpdateCommunityRestoreButtonVisibility();
            CommunityStatusText().Text(L"正在重新合并社区规则…");
            RetryFetchButton().Visibility(Visibility::Collapsed);

            PopupBlocker::FetchCommunityRulesAsync();
        }
    }

    void PopupBlockerPage::UpdateCommunityRestoreButtonVisibility()
    {
        bool hasTombs = false;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            hasTombs = !PopupBlocker::CommunityRemoved.empty();
        }
        if (RestoreCommunityButton()) {
            RestoreCommunityButton().Visibility(hasTombs ? Visibility::Visible : Visibility::Collapsed);
        }
    }
}