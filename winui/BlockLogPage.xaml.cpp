#pragma once
#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "FilePicker.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <sstream>
#include <winrt/Windows.UI.Text.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#if __has_include("BlockLogPage.g.cpp")
#include "BlockLogPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    // 展开行时最多显示的最近记录条数
    constexpr uint32_t kMaxSubRows = 100;

    // 增量读取分块大小（字节）
    constexpr size_t kChunkBuf = 65536;

    // 图标列宽（逻辑像素）
    constexpr double kColIconWidth = 20.0;

    // 行内图标按钮尺寸 / chevron 列宽（逻辑像素）
    constexpr double kIconButtonSize = 24.0;

    // 判定“已置顶”的垂直偏移阈值（逻辑像素）
    constexpr double kTopEpsilon = 4.0;

    // 从 offset 读到文件尾，只保留到最后一个换行为止（末尾没写完的半行留给下次），
    // consumed 返回对齐到整行后的新偏移，供增量读取接着往后读。
    std::wstring ReadLogTextFrom(uint64_t offset, uint64_t& consumed)
    {
        consumed = 0;
        HANDLE hf = ::CreateFileW(PopupBlocker::LogPath().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER pos{}; pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!::SetFilePointerEx(hf, pos, nullptr, FILE_BEGIN)) { ::CloseHandle(hf); return {}; }
        std::string buf;
        char chunk[kChunkBuf];
        DWORD rd{};
        while (::ReadFile(hf, chunk, sizeof(chunk), &rd, nullptr) && rd > 0) buf.append(chunk, rd);
        ::CloseHandle(hf);
        size_t lastNl = buf.rfind('\n');
        if (lastNl == std::string::npos) return {};
        buf.resize(lastNl + 1);
        consumed = offset + buf.size();
        int len = static_cast<int>(buf.size());
        int need = ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), len, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need), 0);
        ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), len, wide.data(), need);
        if (!wide.empty() && wide[0] == 0xFEFF) wide.erase(0, 1);
        return wide;
    }

    uint64_t LogWriteTime()
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!::GetFileAttributesExW(PopupBlocker::LogPath().c_str(), GetFileExInfoStandard, &a)) return 0;
        ULARGE_INTEGER u{};
        u.LowPart = a.ftLastWriteTime.dwLowDateTime;
        u.HighPart = a.ftLastWriteTime.dwHighDateTime;
        return u.QuadPart;
    }

    inline void ReplaceAll(std::wstring& s, const wchar_t* from, const wchar_t* to)
    {
        std::wstring f = from, t = to;
        size_t p = 0;
        while ((p = s.find(f, p)) != std::wstring::npos) { s.replace(p, f.size(), t); p += t.size(); }
    }

    inline void TranslateTokenName(std::wstring& s, const wchar_t* en, const wchar_t* zh)
    {
        std::wstring f = en, t = zh;
        size_t p = 0;
        while ((p = s.find(f, p)) != std::wstring::npos) {
            bool leftOk = (p == 0) || s[p - 1] == L' ';
            size_t e = p + f.size();
            bool rightOk = e < s.size() && (s[e] == L'+' || s[e] == L'-');
            if (leftOk && rightOk) { s.replace(p, f.size(), t); p += t.size(); }
            else p = e;
        }
    }

    inline std::wstring FormatLogLineChinese(std::wstring s)
    {
        ReplaceAll(s, L"action=monitor", L"动作=监控");
        ReplaceAll(s, L"action=allow", L"动作=放行");
        ReplaceAll(s, L"action=block", L"动作=拦截");
        ReplaceAll(s, L"ev=SHOW", L"事件=出现");
        ReplaceAll(s, L"ev=FG", L"事件=焦点");
        ReplaceAll(s, L"reason=heuristic(", L"原因=启发式(");
        ReplaceAll(s, L"reason=whitelist", L"原因=白名单");
        ReplaceAll(s, L"reason=blacklist", L"原因=黑名单");
        ReplaceAll(s, L"reason=heuristic", L"原因=启发式");
        ReplaceAll(s, L"reason=heuristic_off", L"原因=启发式关闭");
        ReplaceAll(s, L"reason=focus_steal", L"原因=焦点窃取");
        ReplaceAll(s, L"raw=", L"特征=");
        ReplaceAll(s, L"ml=Y", L"ML=是");
        ReplaceAll(s, L"ml=N", L"ML=否");
        ReplaceAll(s, L"ml=-", L"ML=跳过");
        ReplaceAll(s, L"infra_class_skip", L"基础设施类:跳过");
        ReplaceAll(s, L"zero_size_skip", L"零尺寸:跳过");
        TranslateTokenName(s, L"mouse_close", L"靠近鼠标");
        TranslateTokenName(s, L"idle", L"用户空闲");
        TranslateTokenName(s, L"far_mouse", L"远离鼠标");
        TranslateTokenName(s, L"notresizable", L"不可调");
        TranslateTokenName(s, L"nominmax", L"无最小最大化");
        TranslateTokenName(s, L"unsigned", L"无签名");
        TranslateTokenName(s, L"resizable", L"可调");
        TranslateTokenName(s, L"minmax", L"最小最大化");
        TranslateTokenName(s, L"capsys", L"标题栏");
        TranslateTokenName(s, L"notitle", L"无标题");
        TranslateTokenName(s, L"toolwin", L"工具窗");
        TranslateTokenName(s, L"topmost", L"置顶");
        TranslateTokenName(s, L"noact", L"不激活");
        TranslateTokenName(s, L"hexclass", L"十六进制类名");
        TranslateTokenName(s, L"signed", L"有签名");
        TranslateTokenName(s, L"young", L"新进程");
        TranslateTokenName(s, L"roaming", L"漫游目录");
        TranslateTokenName(s, L"owner", L"有属主");
        TranslateTokenName(s, L"small", L"小窗");
        TranslateTokenName(s, L"large", L"大窗");
        TranslateTokenName(s, L"temp", L"临时目录");
        TranslateTokenName(s, L"parent_explorer", L"父进程:资源管理器");
        TranslateTokenName(s, L"parent_system", L"父进程:系统");
        TranslateTokenName(s, L"parent_unknown", L"父进程:未知");
        TranslateTokenName(s, L"same_fg", L"同前台进程");
        return s;
    }

    std::wstring ExtractVal(std::wstring const& raw, std::wstring const& key) {
        size_t pos = raw.find(key + L"=");
        if (pos == std::wstring::npos) return L"";
        pos += key.size() + 1;
        size_t end = raw.find(L" |", pos);
        if (end == std::wstring::npos) end = raw.size();
        return raw.substr(pos, end - pos);
    }

    Media::SolidColorBrush MakeBrush(uint8_t r, uint8_t g, uint8_t b)
    {
        return Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, r, g, b });
    }
    Media::SolidColorBrush BrushOk() { return MakeBrush(0x0F, 0x7B, 0x0F); }
    Media::SolidColorBrush BrushBad() { return MakeBrush(0xC4, 0x2B, 0x1C); }
    Media::SolidColorBrush BrushDim() { return MakeBrush(0x80, 0x80, 0x80); }
    Media::SolidColorBrush BrushLine() { return MakeBrush(0x4A, 0x4A, 0x4A); }
    Media::SolidColorBrush BrushLineStrong() { return MakeBrush(0x7A, 0x7A, 0x7A); }
}

namespace winrt::winui::implementation
{
    BlockLogPage::BlockLogPage()
    {
        InitializeComponent();
        this->NavigationCacheMode(Navigation::NavigationCacheMode::Disabled);
        Load();

        m_timer = DispatcherTimer();
        m_timer.Interval(std::chrono::seconds(1));
        m_timer.Tick({ get_weak(), &BlockLogPage::Timer_Tick });
        m_timer.Start();
    }

    BlockLogPage::~BlockLogPage()
    {
        if (m_timer) m_timer.Stop();
    }

    void BlockLogPage::OnLogListLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        std::function<DependencyObject(DependencyObject)> walk =
            [&](DependencyObject d) -> DependencyObject {
            if (!d) return nullptr;
            if (auto sv = d.try_as<Controls::ScrollViewer>()) return sv;
            int n = Media::VisualTreeHelper::GetChildrenCount(d);
            for (int i = 0; i < n; ++i) {
                auto r = walk(Media::VisualTreeHelper::GetChild(d, i));
                if (r) return r;
            }
            return nullptr;
            };
        auto found = walk(LogList());
        m_logScrollViewer = found ? found.try_as<Controls::ScrollViewer>() : nullptr;

        if (m_logScrollViewer) {
            m_logScrollViewer.ViewChanged([this](IInspectable const&,
                Controls::ScrollViewerViewChangedEventArgs const&) {
                    if (m_inApplyFilter || !m_logScrollViewer) return;
                    bool nowPinned = m_logScrollViewer.VerticalOffset() < kTopEpsilon;
                    if (nowPinned && !m_pinnedToTop) {
                        m_pendingNewCount = 0;
                        SyncJumpButton();
                        ApplyFilter();
                    }
                    m_pinnedToTop = nowPinned;
                });
        }
    }

    std::wstring BlockLogPage::CurrentFilterTag()
    {
        std::wstring filterTag = L"all";
        if (FilterCombo() && FilterCombo().SelectedItem()) {
            if (auto item = FilterCombo().SelectedItem().try_as<Controls::ComboBoxItem>()) {
                if (auto tagObj = item.Tag()) filterTag = winrt::unbox_value<winrt::hstring>(tagObj).c_str();
            }
        }
        return filterTag;
    }

    std::wstring BlockLogPage::CurrentSearchText()
    {
        std::wstring searchText;
        if (SearchBox()) {
            searchText = hstring(SearchBox().Text()).c_str();
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::towlower);
        }
        return searchText;
    }

    std::wstring BlockLogPage::ExtractKey(std::wstring const& raw, LogGroup& g)
    {
        g.action = ExtractVal(raw, L"action");
        g.ev = ExtractVal(raw, L"ev");
        g.reason = ExtractVal(raw, L"reason");
        g.exe = ExtractVal(raw, L"exe");
        g.title = ExtractVal(raw, L"title");
        g.cls = ExtractVal(raw, L"class");
        if (g.reason.find(L"heuristic") == 0) g.reason = L"heuristic";

        if (raw.find(L" ml=Y") != std::wstring::npos) g.ml = L"Y";
        else if (raw.find(L" ml=N") != std::wstring::npos) g.ml = L"N";
        else if (raw.find(L" ml=-") != std::wstring::npos) g.ml = L"-";
        g.mlY = (g.ml == L"Y");
        auto heurPos = raw.find(L"heuristic(");
        if (heurPos != std::wstring::npos) {
            size_t endPos = raw.find(L')', heurPos);
            if (endPos != std::wstring::npos) {
                try { g.score = std::stoi(raw.substr(heurPos + 10, endPos - heurPos - 10)); }
                catch (...) {}
            }
        }

        std::wstring timeStr = raw.substr(0, 19);
        g.lastTime = (timeStr.size() >= 19) ? timeStr : L"—";
        if (g.firstTime.empty()) g.firstTime = g.lastTime;

        return g.action + L"|" + g.reason + L"|" + g.exe + L"|" + g.title;
    }

    void BlockLogPage::SetLabelIcon(Controls::FontIcon const& icon, std::wstring const& raw)
    {
        if (auto it = m_labels.find(raw); it != m_labels.end()) {
            if (it->second.label == L"popup") { icon.Glyph(L"\xE73E"); icon.Foreground(BrushOk()); }
            else if (it->second.label == L"notpopup") { icon.Glyph(L"\xE711"); icon.Foreground(BrushBad()); }
            else { icon.Glyph(L""); icon.Foreground(BrushDim()); }
        }
        else { icon.Glyph(L""); icon.Foreground(BrushDim()); }
    }

    std::wstring BlockLogPage::ResolveRawFromTag(IInspectable const& tag)
    {
        if (!tag) return {};
        if (auto v = tag.try_as<winrt::Windows::Foundation::IReference<uint64_t>>())
        {
            size_t g = static_cast<size_t>(v.Value());
            return g < m_groups.size() ? m_groups[g].lastRaw : std::wstring{};
        }
        if (auto v = tag.try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>())
            return std::wstring(v.Value());
        return {};
    }

    Controls::MenuFlyout BlockLogPage::BuildMenu(IInspectable const& tagValue)
    {
        auto fly = Controls::MenuFlyout();
        auto mk = [&](std::wstring const& text, RoutedEventHandler const& h) {
            auto item = Controls::MenuFlyoutItem();
            item.Text(text);
            item.Tag(tagValue);          // 直接转发，调用方决定存 gidx 还是 raw
            item.Click(h);
            fly.Items().Append(item);
            };
        mk(L"标记为弹窗（拦截正确）", { this, &BlockLogPage::MarkPopup_Click });
        mk(L"标记为误关（非弹窗）", { this, &BlockLogPage::MarkNotPopup_Click });
        fly.Items().Append(Controls::MenuFlyoutSeparator());
        mk(L"添加到黑名单", { this, &BlockLogPage::AddToBlacklist_Click });
        mk(L"添加到白名单", { this, &BlockLogPage::AddToWhitelist_Click });
        return fly;
    }

    void BlockLogPage::RowChevronClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto btn = sender.try_as<Controls::Button>()) {
            if (auto tag = btn.Tag()) {
                size_t gidx = static_cast<size_t>(winrt::unbox_value<uint64_t>(tag));
                if (gidx < m_groups.size()) ToggleExpand(gidx);
            }
        }
    }

    Controls::StackPanel BlockLogPage::BuildSubRow(std::wstring const& raw)
    {
        auto wrap = Controls::StackPanel();

        auto grid = Controls::Grid();
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(70, GridUnitType::Pixel));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(44, GridUnitType::Pixel));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(kColIconWidth, GridUnitType::Pixel));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(3).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(4).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        grid.ColumnDefinitions().GetAt(5).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        grid.Margin(ThicknessHelper::FromUniformLength(0));
        grid.Margin(ThicknessHelper::FromLengths(28, 0, 0, 0));
        grid.Padding(ThicknessHelper::FromLengths(0, 2, 0, 2));

        std::wstring t = raw.substr(0, 19);
        auto tbTime = Controls::TextBlock();
        tbTime.Text(t.size() >= 19 ? t.substr(11, 8) : L"");
        tbTime.FontFamily(Media::FontFamily(L"Consolas"));
        tbTime.FontSize(11);
        tbTime.Foreground(BrushDim());
        Controls::Grid::SetColumn(tbTime, 0);
        grid.Children().Append(tbTime);

        std::wstring ev = ExtractVal(raw, L"ev");
        auto tbEv = Controls::TextBlock();
        tbEv.Text((ev == L"SHOW") ? L"出现" : (ev == L"FG") ? L"焦点" : ev);
        tbEv.FontSize(11);
        tbEv.Foreground(BrushDim());
        Controls::Grid::SetColumn(tbEv, 1);
        grid.Children().Append(tbEv);

        auto icon = Controls::FontIcon();
        icon.FontSize(11);
        SetLabelIcon(icon, raw);
        Controls::Grid::SetColumn(icon, 2);
        grid.Children().Append(icon);

        auto tbReason = Controls::TextBlock();
        tbReason.Text(FormatLogLineChinese(L"reason=" + ExtractVal(raw, L"reason")));
        tbReason.FontSize(11);
        Controls::Grid::SetColumn(tbReason, 3);
        grid.Children().Append(tbReason);

        size_t p1 = raw.find(L"reason=");
        std::wstring rest;
        if (p1 != std::wstring::npos) {
            size_t rEnd = raw.find(L" | ", p1 + 7);
            size_t p2 = raw.find(L" | title=");
            size_t stop = (p2 == std::wstring::npos) ? raw.size() : p2;
            if (rEnd != std::wstring::npos && rEnd + 3 < stop) rest = raw.substr(rEnd + 3, stop - rEnd - 3);
        }
        auto tbDetail = Controls::TextBlock();
        tbDetail.Text(FormatLogLineChinese(rest));
        tbDetail.FontFamily(Media::FontFamily(L"Consolas"));
        tbDetail.FontSize(11);
        tbDetail.Foreground(BrushLineStrong());
        tbDetail.TextWrapping(TextWrapping::Wrap);
        Controls::Grid::SetColumn(tbDetail, 4);
        grid.Children().Append(tbDetail);

        auto menuBtn = Controls::Button();
        menuBtn.Content(box_value(hstring(L"⋯")));
        menuBtn.Padding(ThicknessHelper::FromUniformLength(0));
        menuBtn.MinWidth(kIconButtonSize);
        menuBtn.MinHeight(kIconButtonSize);
        menuBtn.FontSize(11);
        menuBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));
        menuBtn.BorderThickness(ThicknessHelper::FromUniformLength(0));
        menuBtn.Flyout(BuildMenu(box_value(hstring(raw))));
        Controls::Grid::SetColumn(menuBtn, 5);
        grid.Children().Append(menuBtn);

        // 右键：ContextFlyout
        grid.ContextFlyout(BuildMenu(box_value(hstring(raw))));

        auto line = Shapes::Rectangle();
        line.Height(1);
        line.Opacity(0.4);
        line.Fill(BrushLine());
        wrap.Children().Append(line);
        wrap.Children().Append(grid);

        return wrap;
    }

    Media::SolidColorBrush BlockLogPage::ResBrush(wchar_t const* key)
    {
        return Resources().Lookup(box_value(hstring(key))).as<Media::SolidColorBrush>();
    }

    std::wstring BlockLogPage::ActionSentence(std::wstring const& action) const
    {
        if (action == L"block") return L"已拦截弹窗";
        if (action == L"allow") return L"已放行";
        return L"已监控（未处理）";
    }

    std::wstring BlockLogPage::ReasonLabel(std::wstring const& reason) const
    {
        if (reason == L"whitelist") return L"白名单";
        if (reason == L"blacklist") return L"黑名单";
        if (reason == L"focus_steal") return L"焦点窃取";
        if (reason == L"heuristic_off") return L"启发式已关闭";
        if (reason.rfind(L"heuristic", 0) == 0) return L"启发式";
        return reason;
    }

    std::wstring BlockLogPage::CalloutText(LogGroup const& g) const
    {
        if (g.action == L"allow" || g.reason == L"whitelist") return L"命中白名单规则，已放行该窗口。";
        if (g.action != L"block") {
            if (g.reason.rfind(L"heuristic", 0) == 0) return L"启发式评分未达阈值，仅记录、未处理。";
            return L"仅记录该窗口的评估结果，未做处理。";
        }
        if (g.reason == L"focus_steal") return L"检测到焦点窃取，已阻止该窗口抢占前台。";
        if (g.reason.rfind(L"heuristic", 0) == 0) return L"启发式评分超过阈值，已关闭该窗口。";
        if (g.reason == L"blacklist") return L"命中黑名单规则，已关闭该窗口。";
        return L"已处理该窗口。";
    }

    std::wstring BlockLogPage::HintText(LogGroup const& g) const
    {
        if (g.action == L"allow") return L"白名单优先于黑名单；如需拦截该窗口，请先移除对应的白名单规则。";
        if (g.action == L"block") return L"若该窗口不应被关闭，可在卡片上右键将它加入白名单。";
        return L"当前仅记录、未做处理；开启「启发式拦截」后超阈值的窗口会被自动关闭。";
    }

    Controls::Grid BlockLogPage::BuildDetailRow(std::wstring const& label, std::wstring const& value)
    {
        auto grid = Controls::Grid();
        grid.ColumnSpacing(12);

        auto c0 = Controls::ColumnDefinition();
        c0.Width(GridLengthHelper::FromValueAndType(96, GridUnitType::Pixel));
        auto c1 = Controls::ColumnDefinition();
        c1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.ColumnDefinitions().Append(c0);
        grid.ColumnDefinitions().Append(c1);

        auto lb = Controls::TextBlock();
        lb.FontSize(14);
        lb.Foreground(ResBrush(L"LogSubFgBrush"));
        lb.Text(label);

        auto vl = Controls::TextBlock();
        vl.FontSize(14);
        vl.TextWrapping(TextWrapping::Wrap);
        vl.IsTextSelectionEnabled(true);
        vl.Text(value);
        Controls::Grid::SetColumn(vl, 1);

        grid.Children().Append(lb);
        grid.Children().Append(vl);
        return grid;
    }

    // 首次展开时创建展开区控件（懒加载）
    void BlockLogPage::EnsureExpandUi(RowUi& ui)
    {
        if (ui.callout) return;

        ui.callout = Controls::InfoBar();
        ui.callout.IsOpen(true);
        ui.callout.IsClosable(false);
        ui.callout.Severity(Controls::InfoBarSeverity::Informational);
        ui.subPanel.Children().Append(ui.callout);

        ui.detailPanel = Controls::StackPanel();
        ui.detailPanel.Spacing(4);
        ui.subPanel.Children().Append(ui.detailPanel);

        ui.hintText = Controls::TextBlock();
        ui.hintText.FontSize(12);
        ui.hintText.Foreground(ResBrush(L"LogSubFgBrush"));
        ui.hintText.TextWrapping(TextWrapping::Wrap);
        ui.subPanel.Children().Append(ui.hintText);

        auto link = Controls::HyperlinkButton();
        link.Content(box_value(hstring(L"打开规则页")));
        link.Padding(ThicknessHelper::FromUniformLength(0));
        link.Click({ this, &BlockLogPage::OpenRules_Click });
        ui.subPanel.Children().Append(link);

        ui.rawPanel = Controls::StackPanel();
        ui.rawPanel.Spacing(2);
        ui.subPanel.Children().Append(ui.rawPanel);
    }

    void BlockLogPage::UpdateRowUi(RowUi& ui, LogGroup const& g)
    {
        bool block = (g.action == L"block");
        bool allow = (g.action == L"allow");
        ui.actionIcon.Glyph(block ? L"\xE72E" : allow ? L"\xE73E" : L"\xE7B3");
        ui.actionIcon.Foreground(block ? BrushBad() : allow ? BrushOk() : BrushDim());

        ui.titleText.Text(ActionSentence(g.action));

        std::wstring sub = g.lastTime;
        if (!g.exe.empty()) sub += L" · " + g.exe;
        if (g.count > 1) sub += L" · 共 " + std::to_wstring(g.count) + L" 次";
        ui.subText.Text(sub);

        ui.tagText.Text(ReasonLabel(g.reason));
        SetLabelIcon(ui.labelIcon, g.lastRaw);

        ui.rot.Angle(g.expanded ? 180 : 0);
        ui.subPanel.Visibility(g.expanded ? Visibility::Visible : Visibility::Collapsed);
        if (!g.expanded) return;

        EnsureExpandUi(ui);
        ui.callout.Message(hstring(CalloutText(g)));

        ui.detailPanel.Children().Clear();
        auto title = Controls::TextBlock();
        title.FontSize(14);
        title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        title.Text(L"详细信息：");
        ui.detailPanel.Children().Append(title);

        if (!g.exe.empty())   ui.detailPanel.Children().Append(BuildDetailRow(L"进程", g.exe));
        if (!g.title.empty()) ui.detailPanel.Children().Append(BuildDetailRow(L"窗口标题", g.title));
        if (!g.cls.empty())   ui.detailPanel.Children().Append(BuildDetailRow(L"窗口类名", g.cls));
        ui.detailPanel.Children().Append(BuildDetailRow(L"判定依据", ReasonLabel(g.reason)));
        if (g.reason.rfind(L"heuristic", 0) == 0)
            ui.detailPanel.Children().Append(BuildDetailRow(L"启发式分数", std::to_wstring(g.score)));
        if (g.reason.rfind(L"heuristic", 0) == 0) {
            std::wstring mlText = (g.ml == L"Y") ? L"是（弹窗）"
                : (g.ml == L"N") ? L"否"
                : (g.ml == L"-") ? L"跳过（ML 未启用或未参与）"
                : L"未记录";
            ui.detailPanel.Children().Append(BuildDetailRow(L"ML 判定", mlText));
        }
        if (g.count > 1)
            ui.detailPanel.Children().Append(BuildDetailRow(L"时间范围", g.firstTime + L" ~ " + g.lastTime));

        ui.hintText.Text(HintText(g));
    }

    RowUi BlockLogPage::BuildRow(size_t gidx)
    {
        RowUi ui;
        ui.root = Controls::Border();
        ui.root.Background(ResBrush(L"LogCardBgBrush"));
        ui.root.BorderBrush(ResBrush(L"LogCardStrokeBrush"));
        ui.root.BorderThickness(ThicknessHelper::FromUniformLength(1));
        ui.root.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        ui.root.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));
        ui.root.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
        ui.root.Tag(box_value(static_cast<uint64_t>(gidx)));
        ui.root.ContextFlyout(BuildMenu(box_value(static_cast<uint64_t>(gidx))));

        ui.body = Controls::StackPanel();

        // 头部：[图标][标题/时间][原因][箭头]
        auto head = Controls::Grid();
        head.ColumnSpacing(12);
        auto cdIcon = Controls::ColumnDefinition();
        cdIcon.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        auto cdText = Controls::ColumnDefinition();
        cdText.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto cdTag = Controls::ColumnDefinition();
        cdTag.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        auto cdBtn = Controls::ColumnDefinition();
        cdBtn.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        head.ColumnDefinitions().Append(cdIcon);
        head.ColumnDefinitions().Append(cdText);
        head.ColumnDefinitions().Append(cdTag);
        head.ColumnDefinitions().Append(cdBtn);

        ui.actionIcon = Controls::FontIcon();
        ui.actionIcon.FontSize(20);
        ui.actionIcon.Margin(ThicknessHelper::FromLengths(4, 2, 4, 0));
        ui.actionIcon.VerticalAlignment(VerticalAlignment::Top);
        Controls::Grid::SetColumn(ui.actionIcon, 0);
        head.Children().Append(ui.actionIcon);

        auto textCol = Controls::StackPanel();
        textCol.Spacing(2);
        ui.titleText = Controls::TextBlock();
        ui.titleText.FontSize(14);
        ui.titleText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        ui.titleText.TextTrimming(TextTrimming::CharacterEllipsis);
        ui.subText = Controls::TextBlock();
        ui.subText.FontSize(12);
        ui.subText.Foreground(ResBrush(L"LogSubFgBrush"));
        ui.subText.TextTrimming(TextTrimming::CharacterEllipsis);
        textCol.Children().Append(ui.titleText);
        textCol.Children().Append(ui.subText);
        Controls::Grid::SetColumn(textCol, 1);
        head.Children().Append(textCol);

        auto tagCol = Controls::StackPanel();
        tagCol.Orientation(Controls::Orientation::Horizontal);
        tagCol.Spacing(6);
        tagCol.VerticalAlignment(VerticalAlignment::Center);
        ui.labelIcon = Controls::FontIcon();
        ui.labelIcon.FontSize(12);
        ui.tagText = Controls::TextBlock();
        ui.tagText.FontSize(12);
        ui.tagText.Foreground(ResBrush(L"LogSubFgBrush"));
        tagCol.Children().Append(ui.labelIcon);
        tagCol.Children().Append(ui.tagText);
        Controls::Grid::SetColumn(tagCol, 2);
        head.Children().Append(tagCol);

        ui.chevronBtn = Controls::Button();
        ui.chevronBtn.Padding(ThicknessHelper::FromUniformLength(0));
        ui.chevronBtn.MinWidth(kIconButtonSize);
        ui.chevronBtn.MinHeight(kIconButtonSize);
        ui.chevronBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));
        ui.chevronBtn.BorderThickness(ThicknessHelper::FromUniformLength(0));
        ui.chevronBtn.Tag(box_value(static_cast<uint64_t>(gidx)));
        ui.chevronBtn.Click({ this, &BlockLogPage::RowChevronClick });

        ui.chevron = Controls::FontIcon();
        ui.chevron.Glyph(L"\xE70D");
        ui.chevron.FontSize(12);
        ui.rot = Media::RotateTransform();
        ui.chevron.RenderTransform(ui.rot);
        ui.chevron.RenderTransformOrigin(winrt::Windows::Foundation::Point{ 0.5f, 0.5f });
        ui.chevronBtn.Content(ui.chevron);
        Controls::Grid::SetColumn(ui.chevronBtn, 3);
        head.Children().Append(ui.chevronBtn);

        ui.body.Children().Append(head);

        // 展开区：提示条 + 详细信息 + 说明 + 链接 + 事件记录
        ui.subPanel = Controls::StackPanel();
        ui.subPanel.Spacing(12);
        ui.subPanel.Margin(ThicknessHelper::FromLengths(32, 12, 0, 4));
        ui.subPanel.Visibility(Visibility::Collapsed);
        // 展开区内容（InfoBar / 详细信息 / 说明 / 链接 / 事件记录）延迟到首次展开时创建，
        // 避免每行都预建十多个控件（300 行约省 20–40 MB）

        ui.body.Children().Append(ui.subPanel);
        ui.root.Child(ui.body);

        return ui;
    }

    void BlockLogPage::ToggleExpand(size_t gidx)
    {
        auto& g = m_groups[gidx];
        g.expanded = !g.expanded; // 允许单次记录展开

        // 找该行 UI
        RowUi* ui = nullptr;
        for (size_t i = 0; i < m_visibleGroups.size(); ++i) {
            if (m_visibleGroups[i] == gidx && i < m_rows.size()) { ui = &m_rows[i]; break; }
        }
        if (!ui) return;

        if (g.expanded) {
            EnsureExpandUi(*ui);
            ui->rawPanel.Children().Clear();
            size_t n = g.raws.size();
            size_t from = (n > kMaxSubRows) ? n - kMaxSubRows : 0;
            for (size_t i = n; i-- > from; ) ui->rawPanel.Children().Append(BuildSubRow(g.raws[i]));
            if (n > kMaxSubRows) {
                auto tip = Controls::TextBlock();
                tip.Text(std::wstring(L"… 仅显示最近 ") + std::to_wstring(kMaxSubRows) + L" 条事件 …");
                tip.FontSize(11);
                tip.Foreground(BrushDim());
                ui->rawPanel.Children().Append(tip);
            }
        }
        UpdateRowUi(*ui, g);
    }

    bool BlockLogPage::GroupPassFilter(LogGroup const& g, std::wstring const& filterTag, std::wstring const& searchText, int threshold)
    {
        if (!searchText.empty()) {
            std::wstring lowerExe = g.exe, lowerTitle = g.title, lowerCls = g.cls, lowerSearch = searchText;
            std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);
            std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);
            std::transform(lowerCls.begin(), lowerCls.end(), lowerCls.begin(), ::towlower);
            if (lowerExe.find(lowerSearch) == std::wstring::npos &&
                lowerTitle.find(lowerSearch) == std::wstring::npos &&
                lowerCls.find(lowerSearch) == std::wstring::npos)
                return false;
        }
        if (filterTag == L"all") return true;
        if (filterTag == L"action_block")   return g.action == L"block";
        if (filterTag == L"action_allow")   return g.action == L"allow";
        if (filterTag == L"action_monitor") return g.action == L"monitor";
        if (filterTag == L"list") return (g.reason == L"whitelist" || g.reason == L"blacklist");
        if (filterTag == L"ml_heur" || filterTag == L"ml_list") {
            // ML 跳过/未记录不属于「ML 冲突」
            if (g.ml != L"Y" && g.ml != L"N") return false;
        }
        if (filterTag == L"ml_heur") {
            if (g.reason == L"heuristic") return (g.score >= threshold) != g.mlY;
            return false;
        }
        if (filterTag == L"ml_list") {
            if (g.reason == L"whitelist") return g.mlY;
            if (g.reason == L"blacklist") return !g.mlY;
            return false;
        }
        return true;
    }

    void BlockLogPage::SyncJumpButton()
    {
        if (!NewLogJumpButton()) return;
        if (m_pendingNewCount == 0) NewLogJumpButton().Visibility(Visibility::Collapsed);
        else {
            NewLogJumpButton().Content(box_value(hstring(L"↑ 有新日志 (" + std::to_wstring(m_pendingNewCount) + L")")));
            NewLogJumpButton().Visibility(Visibility::Visible);
        }
    }

    void BlockLogPage::UpdateCountText()
    {
        if (FilterCountText()) {
            FilterCountText().Text(hstring(std::to_wstring(m_visibleGroups.size()) +
                L" / " + std::to_wstring(m_groups.size()) + L" 条"));
        }
    }

    void BlockLogPage::ReloadFromFile()
    {
        uint64_t consumed = 0;
        std::wstringstream ss(ReadLogTextFrom(0, consumed));
        if (consumed > 0) m_lastFileSize = consumed;
        std::wstring line;
        m_groups.clear();
        m_groupIndex.clear();
        m_seq = 0;

        while (std::getline(ss, line, L'\n')) {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (line.empty()) continue;

            LogGroup g;
            std::wstring key = ExtractKey(line, g);
            g.count = 1;
            g.lastRaw = line;
            g.raws.push_back(line);

            auto it = m_groupIndex.find(key);
            if (it != m_groupIndex.end()) {
                auto& tgt = m_groups[it->second];
                tgt.count++;
                tgt.lastTime = g.lastTime;
                tgt.lastRaw = line;
                tgt.raws.push_back(line);
                tgt.seq = ++m_seq;
            }
            else {
                g.seq = ++m_seq;
                m_groupIndex[key] = m_groups.size();
                m_groups.push_back(g);
            }
        }
    }

    void BlockLogPage::ApplyFilter()
    {
        if (!LogList()) return;
        m_inApplyFilter = true;
        LogList().Items().Clear();
        m_visibleGroups.clear();
        m_rows.clear();

        std::wstring filterTag = CurrentFilterTag();
        std::wstring searchText = CurrentSearchText();
        int threshold = PopupBlocker::HeuristicThreshold;

        std::vector<size_t> passIdx;
        for (size_t i = 0; i < m_groups.size(); ++i)
            if (GroupPassFilter(m_groups[i], filterTag, searchText, threshold)) passIdx.push_back(i);
        std::stable_sort(passIdx.begin(), passIdx.end(),
            [this](size_t a, size_t b) { return m_groups[a].seq > m_groups[b].seq; });

        for (size_t i : passIdx) {
            m_visibleGroups.push_back(i);
            auto ui = BuildRow(i);
            UpdateRowUi(ui, m_groups[i]);
            if (m_groups[i].expanded) {
                EnsureExpandUi(ui);
                size_t n = m_groups[i].raws.size();
                size_t from = (n > kMaxSubRows) ? n - kMaxSubRows : 0;
                for (size_t r = n; r-- > from; ) ui.rawPanel.Children().Append(BuildSubRow(m_groups[i].raws[r]));
                ui.subPanel.Visibility(Visibility::Visible);
            }
            m_rows.push_back(ui);
            LogList().Items().Append(ui.root);
        }

        UpdateCountText();
        m_pendingNewCount = 0;
        SyncJumpButton();
        m_inApplyFilter = false;
    }

    void BlockLogPage::AppendNewLines()
    {
        uint64_t consumed = 0;
        std::wstring text = ReadLogTextFrom(m_lastFileSize, consumed);
        if (consumed > m_lastFileSize) m_lastFileSize = consumed;
        if (text.empty()) return;

        std::vector<std::wstring> newLines;
        size_t start = 0;
        while (start < text.size()) {
            size_t nl = text.find(L'\n', start);
            if (nl == std::wstring::npos) nl = text.size();
            std::wstring line = text.substr(start, nl - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            start = nl + 1;
            if (!line.empty()) newLines.push_back(line);
        }
        if (newLines.empty()) return;

        std::wstring filterTag = CurrentFilterTag();
        std::wstring searchText = CurrentSearchText();
        int threshold = PopupBlocker::HeuristicThreshold;

        for (auto& line : newLines) {
            LogGroup g;
            std::wstring key = ExtractKey(line, g);
            g.count = 1;
            g.lastRaw = line;
            g.raws.push_back(line);

            auto it = m_groupIndex.find(key);
            bool isNewGroup = (it == m_groupIndex.end());
            size_t gidx;

            if (!isNewGroup) {
                gidx = it->second;
                auto& tgt = m_groups[gidx];
                tgt.count++;
                tgt.lastTime = g.lastTime;
                tgt.lastRaw = line;
                tgt.raws.push_back(line);
                tgt.seq = ++m_seq;
            }
            else {
                gidx = m_groups.size();
                g.seq = ++m_seq;
                m_groupIndex[key] = gidx;
                m_groups.push_back(g);
            }

            auto& grp = m_groups[gidx];
            bool pass = GroupPassFilter(grp, filterTag, searchText, threshold);

            if (m_pinnedToTop) {
                if (isNewGroup && pass) {
                    m_visibleGroups.insert(m_visibleGroups.begin(), gidx);
                    auto ui = BuildRow(gidx);
                    UpdateRowUi(ui, grp);
                    m_rows.insert(m_rows.begin(), ui);
                    LogList().Items().InsertAt(0, ui.root);
                    if (LogList().Items().Size() > 0) LogList().ScrollIntoView(LogList().Items().GetAt(0));
                }
                else if (!isNewGroup && pass) {
                    long long pos = -1;
                    for (size_t i = 0; i < m_visibleGroups.size(); ++i)
                        if (m_visibleGroups[i] == gidx) { pos = (long long)i; break; }
                    if (pos >= 0) {
                        if (pos > 0) {
                            // 提到最顶：四处同步移动
                            RowUi moved = m_rows[static_cast<size_t>(pos)];
                            m_visibleGroups.erase(m_visibleGroups.begin() + pos);
                            m_rows.erase(m_rows.begin() + pos);
                            LogList().Items().RemoveAt(static_cast<uint32_t>(pos));
                            m_visibleGroups.insert(m_visibleGroups.begin(), gidx);
                            m_rows.insert(m_rows.begin(), moved);
                            LogList().Items().InsertAt(0, moved.root);
                        }
                        UpdateRowUi(m_rows[0], grp);
                        if (grp.expanded) {
                            // 只往「事件记录」面板（rawPanel）里插，不能插到 subPanel：
                            // subPanel 里还有 InfoBar/详细信息/链接等固定子项，按尾部裁剪会把它们删掉
                            EnsureExpandUi(m_rows[0]);
                            auto& rp = m_rows[0].rawPanel;
                            rp.Children().InsertAt(0, BuildSubRow(line));
                            while (rp.Children().Size() > kMaxSubRows)
                                rp.Children().RemoveAt(rp.Children().Size() - 1);
                        }
                        if (LogList().Items().Size() > 0)
                            LogList().ScrollIntoView(LogList().Items().GetAt(0));
                    }
                }
            }
            else {
                if (pass) { m_pendingNewCount++; SyncJumpButton(); }
            }
        }
        UpdateCountText();
    }

    void BlockLogPage::Load()
    {
        PopupBlocker::FlushLog();
        uint64_t t = LogWriteTime();
        if (t != m_lastWrite) {
            m_lastWrite = t;
            ReloadFromFile();
            SampleLabels::Load(m_labels);
            ApplyFilter();
        }
    }

    void BlockLogPage::Timer_Tick(IInspectable const&, IInspectable const&)
    {
        if (PopupBlocker::ShuttingDown.load()) return;
        PopupBlocker::RequestFlushLog();   // 非阻塞：不等待写线程，避免每秒卡住 UI
        auto strongThis = get_strong();
        if (!strongThis) return;

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!::GetFileAttributesExW(PopupBlocker::LogPath().c_str(), GetFileExInfoStandard, &fad)) return;
        uint64_t size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        ULARGE_INTEGER u{};
        u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        uint64_t wt = u.QuadPart;

        if (wt == m_lastWrite && size == m_lastFileSize) return;
        if (size < m_lastFileSize) { m_lastWrite = wt; Load(); return; }
        m_lastWrite = wt;
        AppendNewLines();
    }

    void BlockLogPage::Filter_Changed(IInspectable const&, Controls::SelectionChangedEventArgs const&) { ApplyFilter(); }
    void BlockLogPage::Search_Changed(IInspectable const&, Controls::TextChangedEventArgs const&) { ApplyFilter(); }
    void BlockLogPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&) { Load(); }

    void BlockLogPage::NewLogJumpButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_pendingNewCount = 0;
        SyncJumpButton();
        m_pinnedToTop = true;
        ApplyFilter();
        if (LogList().Items().Size() > 0) LogList().ScrollIntoView(LogList().Items().GetAt(0));
    }

    void BlockLogPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&) { m_lastWrite = 0; Load(); }

    void BlockLogPage::Clear_Click(IInspectable const&, RoutedEventArgs const&)
    {
        PopupBlocker::ClearLog();
        m_lastWrite = 0;
        Load();
    }

    void BlockLogPage::MarkPopup_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto item = sender.try_as<Controls::MenuFlyoutItem>())
            m_selectedRaw = ResolveRawFromTag(item.Tag());
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
        s.label = L"popup";
        m_labels[m_selectedRaw] = s;
        SampleLabels::Save(m_labels);
        m_lastWrite = 0;
        Load();
    }

    void BlockLogPage::MarkNotPopup_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto item = sender.try_as<Controls::MenuFlyoutItem>())
            m_selectedRaw = ResolveRawFromTag(item.Tag());
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
        s.label = L"notpopup";
        m_labels[m_selectedRaw] = s;
        SampleLabels::Save(m_labels);
        m_lastWrite = 0;
        Load();
    }

    void BlockLogPage::ExportSamples_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring path = FilePicker::PickJsonFile(true);
        if (path.empty()) return;
        std::string json = SampleLabels::ExportJson(m_labels);
        if (PopupBlocker::WriteUtf8StringToFile(path, json)) {
            SampleLabels::Clear(m_labels);
            m_lastWrite = 0;
            Load();
            MessageBoxW(nullptr, L"训练数据导出成功，本地缓存已清空", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        else MessageBoxW(nullptr, L"导出失败", L"提示", MB_OK | MB_ICONERROR);
    }

    void BlockLogPage::OpenRules_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto window = winrt::winui::implementation::App::window;
        if (!window) return;
        if (auto mainWindow = window.try_as<winrt::winui::MainWindow>())
            mainWindow.NavigateToTag(L"Blocker");
    }

    void BlockLogPage::AddToBlacklist_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto item = sender.try_as<Controls::MenuFlyoutItem>())
            m_selectedRaw = ResolveRawFromTag(item.Tag());
        AddRuleFromSelection(false);
    }

    void BlockLogPage::AddToWhitelist_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto item = sender.try_as<Controls::MenuFlyoutItem>())
            m_selectedRaw = ResolveRawFromTag(item.Tag());
        AddRuleFromSelection(true);
    }

    void BlockLogPage::AddRuleFromSelection(bool whitelist)
    {
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
        if (s.exe.empty()) {
            MessageBoxW(nullptr, L"该日志缺少进程信息，无法生成规则。", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
        PopupBlocker::Rule r;
        r.isWhitelist = whitelist;
        PopupBlocker::RuleCondition c;
        c.field = PopupBlocker::RuleField::Exe;
        c.mode = PopupBlocker::MatchMode::Exact;
        c.pattern = PopupBlocker::Lower(s.exe);
        r.conditions.push_back(std::move(c));
        r.fromCommunity = false;

        std::wstring k = PopupBlocker::RuleKey(r);
        bool exists = false;
        PopupBlocker::MutateRules([&](std::vector<PopupBlocker::Rule>& rules, std::vector<std::wstring>&) {
            exists = std::any_of(rules.begin(), rules.end(),
                [&k](PopupBlocker::Rule const& e) { return PopupBlocker::RuleKey(e) == k; });
            if (!exists) rules.push_back(r);
        });
        if (exists) { MessageBoxW(nullptr, L"相同规则已存在。", L"提示", MB_OK | MB_ICONINFORMATION); return; }

        MessageBoxW(nullptr, ((whitelist ? L"已添加白名单规则：进程 " : L"已添加黑名单规则：进程 ") + s.exe).c_str(),
            L"提示", MB_OK | MB_ICONINFORMATION);
    }
}