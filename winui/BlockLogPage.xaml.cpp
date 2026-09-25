#pragma once
#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "LabelStorage.h"
#include "FilePicker.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <functional>
#include <ctime>
#include <string>
#if __has_include("BlockLogPage.g.cpp")
#include "BlockLogPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    std::wstring ReadLogText()
    {
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"rb") != 0 || !f) return {};
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
        ::fclose(f);
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);
        int need = ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need) - 1, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, wide.data(), need);
        return wide;
    }

    std::wstring ReadLogTextFrom(uint64_t offset, uint64_t& consumed)
    {
        consumed = 0;
        HANDLE hf = ::CreateFileW(PopupBlocker::LogPath().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER pos{}; pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!::SetFilePointerEx(hf, pos, nullptr, FILE_BEGIN)) { ::CloseHandle(hf); return {}; }
        std::string buf;
        char chunk[65536];
        DWORD rd{};
        while (::ReadFile(hf, chunk, sizeof(chunk), &rd, nullptr) && rd > 0) buf.append(chunk, rd);
        ::CloseHandle(hf);
        size_t lastNl = buf.rfind('\n');
        if (lastNl == std::string::npos) return {};
        buf.resize(lastNl + 1);
        consumed = offset + buf.size();
        int need = ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), -1, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need) - 1, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), -1, wide.data(), need);
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
        ReplaceAll(s, L"action=kill", L"动作=强杀");
        ReplaceAll(s, L"ev=SHOW", L"事件=出现");
        ReplaceAll(s, L"ev=FG", L"事件=焦点");
        ReplaceAll(s, L"reason=heuristic(", L"原因=启发式(");
        ReplaceAll(s, L"reason=whitelist", L"原因=白名单");
        ReplaceAll(s, L"reason=blacklist", L"原因=黑名单");
        ReplaceAll(s, L"reason=heuristic", L"原因=启发式");
        ReplaceAll(s, L"reason=heuristic_off", L"原因=启发式关闭");
        ReplaceAll(s, L"raw=", L"特征=");
        ReplaceAll(s, L"ml=Y", L"ML=是");
        ReplaceAll(s, L"ml=N", L"ML=否");
        ReplaceAll(s, L"ml=-", L"ML=跳过");
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
                    bool nowPinned = m_logScrollViewer.VerticalOffset() < 4.0;
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

        g.mlY = raw.find(L" ml=Y") != std::wstring::npos;
        auto heurPos = raw.find(L"heuristic(");
        if (heurPos != std::wstring::npos) {
            size_t endPos = raw.find(L')', heurPos);
            if (endPos != std::wstring::npos) {
                try { g.score = std::stoi(raw.substr(heurPos + 10, endPos - heurPos - 10)); }
                catch (...) {}
            }
        }

        std::wstring timeStr = raw.substr(0, 19);
        g.lastTime = timeStr.size() >= 19 ? timeStr.substr(11, 8) : L"??:??:??";
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
        grid.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(20, GridUnitType::Pixel));
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        // reason 列改为自适应
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
        // 去截断，改自动换行
        tbDetail.TextWrapping(TextWrapping::Wrap);
        Controls::Grid::SetColumn(tbDetail, 4);
        grid.Children().Append(tbDetail);

        auto menuBtn = Controls::Button();
        menuBtn.Content(box_value(hstring(L"⋯")));
        menuBtn.Padding(ThicknessHelper::FromUniformLength(0));
        menuBtn.MinWidth(24);
        menuBtn.MinHeight(24);
        menuBtn.FontSize(11);
        menuBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));
        menuBtn.BorderThickness(ThicknessHelper::FromUniformLength(0));
        menuBtn.Flyout(BuildMenu(box_value(hstring(raw))));
        Controls::Grid::SetColumn(menuBtn, 5);
        grid.Children().Append(menuBtn);

        // 右键：ContextFlyout（不引入 Input 委托）
        grid.ContextFlyout(BuildMenu(box_value(hstring(raw))));

        // 子行顶部细线
        auto line = Shapes::Rectangle();
        line.Height(1);
        line.Opacity(0.4);
        line.Fill(BrushLine());
        wrap.Children().Append(line);
        wrap.Children().Append(grid);

        return wrap;
    }

    void BlockLogPage::UpdateRowUi(RowUi& ui, LogGroup const& g)
    {
        ui.actionText.Text((g.action == L"block") ? L"拦截" : (g.action == L"allow") ? L"放行"
            : (g.action == L"kill") ? L"强杀" : L"监控");
        ui.chip.Background((g.action == L"block") ? BrushBad()
            : (g.action == L"allow") ? BrushOk()
            : (g.action == L"kill") ? MakeBrush(0x8B, 0x00, 0x00) : MakeBrush(0x61, 0x61, 0x61));

        bool many = g.count > 1;
        // 箭头始终可见，允许展开查看单次拦截的详细特征/ML 结果
        ui.chevronBtn.Visibility(Visibility::Visible);
        // 只有多次触发才显示次数徽章
        ui.badgeBox.Visibility(many ? Visibility::Visible : Visibility::Collapsed);
        if (many) ui.badgeText.Text(std::to_wstring(g.count));
        ui.rot.Angle(g.expanded ? 90 : 0);
        ui.subPanel.Visibility(g.expanded ? Visibility::Visible : Visibility::Collapsed);

        ui.timeText.Text(many ? (g.firstTime + L"~" + g.lastTime) : g.lastTime);
        ui.reasonText.Text(FormatLogLineChinese(L"reason=" + g.reason));
        ui.exeText.Text(g.exe);
        ui.titleText.Text(g.title);
        SetLabelIcon(ui.labelIcon, g.lastRaw);
    }

    RowUi BlockLogPage::BuildRow(size_t gidx)
    {
        RowUi ui;
        ui.root = Controls::Border();
        ui.root.Tag(box_value(static_cast<uint64_t>(gidx)));
        ui.root.BorderThickness(ThicknessHelper::FromLengths(0, 2, 0, 0));
        ui.root.BorderBrush(BrushLineStrong());
        ui.root.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));

        ui.body = Controls::StackPanel();

        auto head = Controls::Grid();
        for (int i = 0; i < 9; ++i) {
            auto cd = Controls::ColumnDefinition();
            if (i == 0) cd.Width(GridLengthHelper::FromValueAndType(24, GridUnitType::Pixel));
            else if (i == 3) cd.Width(GridLengthHelper::FromValueAndType(150, GridUnitType::Pixel));
            else if (i == 4) cd.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            else if (i == 7) cd.Width(GridLengthHelper::FromValueAndType(20, GridUnitType::Pixel));
            else cd.Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
            head.ColumnDefinitions().Append(cd);
        }
        head.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));

        ui.chip = Controls::Border();
        ui.chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        ui.chip.Padding(ThicknessHelper::FromLengths(6, 1, 6, 1));
        ui.chip.VerticalAlignment(VerticalAlignment::Center);
        ui.actionText = Controls::TextBlock();
        ui.actionText.FontSize(11);
        ui.actionText.Foreground(MakeBrush(0xFF, 0xFF, 0xFF));
        ui.chip.Child(ui.actionText);
        Controls::Grid::SetColumn(ui.chip, 1);
        head.Children().Append(ui.chip);

        ui.badgeBox = Controls::Border();
        ui.badgeBox.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        ui.badgeBox.Padding(ThicknessHelper::FromLengths(6, 0, 6, 0));
        ui.badgeBox.MinWidth(16);
        ui.badgeBox.Height(16);
        ui.badgeBox.Background(MakeBrush(0x33, 0x66, 0x99));
        ui.badgeBox.VerticalAlignment(VerticalAlignment::Center);
        ui.badgeBox.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
        ui.badgeText = Controls::TextBlock();
        ui.badgeText.FontSize(10);
        ui.badgeText.Foreground(MakeBrush(0xFF, 0xFF, 0xFF));
        ui.badgeText.HorizontalAlignment(HorizontalAlignment::Center);
        ui.badgeBox.Child(ui.badgeText);
        Controls::Grid::SetColumn(ui.badgeBox, 2);
        head.Children().Append(ui.badgeBox);

        ui.exeText = Controls::TextBlock();
        ui.exeText.FontFamily(Media::FontFamily(L"Consolas"));
        ui.exeText.FontSize(12);
        ui.exeText.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
        ui.exeText.TextTrimming(TextTrimming::CharacterEllipsis);
        Controls::Grid::SetColumn(ui.exeText, 3);
        head.Children().Append(ui.exeText);

        ui.titleText = Controls::TextBlock();
        ui.titleText.FontSize(12);
        ui.titleText.Foreground(BrushDim());
        ui.titleText.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
        ui.titleText.TextTrimming(TextTrimming::CharacterEllipsis);
        Controls::Grid::SetColumn(ui.titleText, 4);
        head.Children().Append(ui.titleText);

        ui.timeText = Controls::TextBlock();
        ui.timeText.FontSize(11);
        ui.timeText.Foreground(BrushDim());
        Controls::Grid::SetColumn(ui.timeText, 5);
        head.Children().Append(ui.timeText);

        ui.reasonText = Controls::TextBlock();
        ui.reasonText.FontSize(11);
        ui.reasonText.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
        Controls::Grid::SetColumn(ui.reasonText, 6);
        head.Children().Append(ui.reasonText);

        ui.labelIcon = Controls::FontIcon();
        ui.labelIcon.FontSize(12);
        Controls::Grid::SetColumn(ui.labelIcon, 7);
        head.Children().Append(ui.labelIcon);

        ui.body.Children().Append(head);

        ui.subPanel = Controls::StackPanel();
        ui.subPanel.Visibility(Visibility::Collapsed);
        ui.body.Children().Append(ui.subPanel);

        ui.root.Child(ui.body);

        // chevron 按钮（第 0 列）
        ui.chevronBtn = Controls::Button();
        ui.chevronBtn.Padding(ThicknessHelper::FromUniformLength(0));
        ui.chevronBtn.MinWidth(24);
        ui.chevronBtn.MinHeight(24);
        ui.chevronBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));
        ui.chevronBtn.BorderThickness(ThicknessHelper::FromUniformLength(0));
        ui.chevronBtn.Tag(box_value(static_cast<uint64_t>(gidx)));
        ui.chevronBtn.Click({ this, &BlockLogPage::RowChevronClick });

        ui.chevron = Controls::FontIcon();
        ui.chevron.Glyph(L"\xE76C");
        ui.chevron.FontSize(12);
        ui.rot = Media::RotateTransform();
        ui.chevron.RenderTransform(ui.rot);
        ui.chevron.RenderTransformOrigin(winrt::Windows::Foundation::Point{ 0.5f, 0.5f });
        ui.chevronBtn.Content(ui.chevron);
        Controls::Grid::SetColumn(ui.chevronBtn, 0);
        head.Children().Append(ui.chevronBtn);

        // 行尾"⋯"按钮（第 8 列）
        auto menuBtn = Controls::Button();
        menuBtn.Content(box_value(hstring(L"⋯")));
        menuBtn.Padding(ThicknessHelper::FromUniformLength(0));
        menuBtn.MinWidth(24);
        menuBtn.MinHeight(24);
        menuBtn.FontSize(12);
        menuBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));
        menuBtn.BorderThickness(ThicknessHelper::FromUniformLength(0));
        menuBtn.Flyout(BuildMenu(box_value(static_cast<uint64_t>(gidx))));
        Controls::Grid::SetColumn(menuBtn, 8);
        head.Children().Append(menuBtn);

        // 聚合行右键：ContextFlyout，Tag 存 gidx
        ui.root.ContextFlyout(BuildMenu(box_value(static_cast<uint64_t>(gidx))));

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

        ui->subPanel.Children().Clear();
        if (g.expanded) {
            size_t n = g.raws.size();
            size_t from = (n > 100) ? n - 100 : 0;
            for (size_t i = n; i-- > from; ) ui->subPanel.Children().Append(BuildSubRow(g.raws[i]));
            if (n > 100) {
                auto tip = Controls::TextBlock();
                tip.Text(L"        … 仅显示最近 100 条 …");
                tip.FontSize(11);
                tip.Foreground(BrushDim());
                ui->subPanel.Children().Append(tip);
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
        std::wstringstream ss(ReadLogText());
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
                size_t n = m_groups[i].raws.size();
                size_t from = (n > 100) ? n - 100 : 0;
                for (size_t r = n; r-- > from; ) ui.subPanel.Children().Append(BuildSubRow(m_groups[i].raws[r]));
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
                            m_rows[0].subPanel.Children().InsertAt(0, BuildSubRow(line));
                            uint32_t cap = 100 + (grp.raws.size() > 100 ? 1 : 0);
                            while (m_rows[0].subPanel.Children().Size() > cap)
                                m_rows[0].subPanel.Children().RemoveAt(m_rows[0].subPanel.Children().Size() - 1);
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
        uint64_t t = LogWriteTime();
        if (t != m_lastWrite) {
            m_lastWrite = t;
            ReloadFromFile();
            SampleLabels::Load(m_labels);
            ApplyFilter();
        }
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (::GetFileAttributesExW(PopupBlocker::LogPath().c_str(), GetFileExInfoStandard, &fad))
            m_lastFileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }

    void BlockLogPage::Timer_Tick(IInspectable const&, IInspectable const&)
    {
        if (PopupBlocker::ShuttingDown.load()) return;
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
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"wb") == 0 && f) ::fclose(f);
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
        r.field = PopupBlocker::RuleField::Exe;
        r.mode = PopupBlocker::MatchMode::Exact;
        r.pattern = PopupBlocker::Lower(s.exe);
        r.fromCommunity = false;

        std::vector<PopupBlocker::Rule> rules;
        { std::lock_guard lock(PopupBlocker::RulesMutex); rules = PopupBlocker::Rules; }

        std::wstring k = PopupBlocker::RuleKey(r);
        bool exists = std::any_of(rules.begin(), rules.end(),
            [&k](PopupBlocker::Rule const& e) { return PopupBlocker::RuleKey(e) == k; });
        if (exists) { MessageBoxW(nullptr, L"相同规则已存在。", L"提示", MB_OK | MB_ICONINFORMATION); return; }

        rules.push_back(r);
        PopupBlocker::SaveRules(rules);
        MessageBoxW(nullptr, ((whitelist ? L"已添加白名单规则：进程 " : L"已添加黑名单规则：进程 ") + s.exe).c_str(),
            L"提示", MB_OK | MB_ICONINFORMATION);
    }
}