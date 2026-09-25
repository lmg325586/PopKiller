#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <queue>
#include <map>
#include <unordered_map>
#include "HeuristicML.h"
#include "AppSettings.h"
#include "HeuristicScorer.h"
#include "RuleTypes.h"
#include "RuleStorage.h"
#include "OwnerFunction.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cstdio>
#include <cwchar>

namespace PopupBlocker
{
    inline std::vector<Rule> Rules;
    inline std::vector<std::wstring> CommunityRemoved;
    // owner_function：携带注册者（owner）指针的回调槽，
    // 使"仅清除自己注册的回调"成为可能（见 ClearCallbackIfOwnedBy）。
    // 社区规则拉取完成回调：由 MainWindow 常驻注册（引擎状态同步），
    // PopupBlockerPage 在显示期间临时注册以刷新 UI，析构时只摘除自己的那份。
    inline owner_function<void(bool, std::wstring)> CommunityRulesFetchCallback;
    inline std::mutex RulesMutex;

    inline std::mutex CallbackMutex;

    template <typename Sig, typename... Args>
    void SafeInvoke(std::function<Sig>& slot, Args&&... args)
    {
        std::function<Sig> f;
        { std::lock_guard lock(CallbackMutex); f = slot; }
        if (f) f(std::forward<Args>(args)...);
    }

    // owner_function 版 SafeInvoke：持锁拷贝出可调用对象后不持锁执行，
    // 避免与回调内部的再次加锁形成死锁/长阻塞。
    template <typename R, typename... S, typename... Args>
    void SafeInvoke(owner_function<R(S...)>& slot, Args&&... args)
    {
        owner_function<R(S...)> f;
        { std::lock_guard lock(CallbackMutex); f = slot; }
        if (f) f(std::forward<Args>(args)...);
    }

    // 仅当槽位中存放的仍是 owner 所注册的回调时才清除，
    // 避免页面析构时误伤 App/MainWindow 常驻管理层注册的回调。
    template <typename R, typename... Args>
    void ClearCallbackIfOwnedBy(owner_function<R(Args...)>& slot, const void* owner)
    {
        std::lock_guard lock(CallbackMutex);
        if (owner && slot.owner() == owner)
            slot = nullptr;
    }

    // 读取槽位当前的注册者指针（用于判断是否已有常驻注册方，避免重复覆盖）。
    template <typename R, typename... Args>
    const void* CallbackOwnerOf(const owner_function<R(Args...)>& slot)
    {
        std::lock_guard lock(CallbackMutex);
        return slot.owner();
    }

    inline std::shared_ptr<const std::vector<Rule>> RulesView =
        std::make_shared<const std::vector<Rule>>();

    struct AcAutomaton
    {
        struct Node { std::map<wchar_t, int> next; int fail = 0; int out = 0; };
        std::vector<Node> nodes{ Node{} };

        void Add(std::wstring const& p, int flags)   // flags: 1=白 2=黑
        {
            int cur = 0;
            for (wchar_t c : p) {
                auto it = nodes[cur].next.find(c);
                if (it == nodes[cur].next.end()) {
                    it = nodes[cur].next.emplace(c, static_cast<int>(nodes.size())).first;
                    nodes.emplace_back();
                }
                cur = it->second;
            }
            nodes[cur].out |= flags;
        }

        void Build()
        {
            std::queue<int> q;
            for (auto& kv : nodes[0].next) { nodes[kv.second].fail = 0; q.push(kv.second); }
            while (!q.empty()) {
                int u = q.front(); q.pop();
                nodes[u].out |= nodes[nodes[u].fail].out;   // 合并 fail 链输出
                for (auto& kv : nodes[u].next) {
                    int v = kv.second;
                    int f = nodes[u].fail;
                    while (f && !nodes[f].next.count(kv.first)) f = nodes[f].fail;
                    auto it = nodes[f].next.find(kv.first);
                    nodes[v].fail = (it != nodes[f].next.end() && it->second != v) ? it->second : 0;
                    q.push(v);
                }
            }
        }

        int Scan(std::wstring const& s) const
        {
            int flags = 0, cur = 0;
            for (wchar_t c : s) {
                while (cur && !nodes[cur].next.count(c)) cur = nodes[cur].fail;
                auto it = nodes[cur].next.find(c);
                cur = (it != nodes[cur].next.end()) ? it->second : 0;
                if (nodes[cur].out) {
                    flags |= nodes[cur].out;
                    if (flags == 3) return flags;
                }
            }
            return flags;
        }
    };

    struct RuleIndex
    {
        std::unordered_map<std::wstring, int> exact[4];
        AcAutomaton contains[4];
        std::vector<Rule> wilds;        // 通配符规则保留遍历
        bool hasExact[4]{};
        bool hasContains[4]{};
        bool hasWild[4]{};
        bool empty = true;
    };

    inline std::shared_ptr<const RuleIndex> BuildRuleIndex(std::vector<Rule> const& rules)
    {
        auto idx = std::make_shared<RuleIndex>();
        for (auto const& r : rules) {
            int f = static_cast<int>(r.field);
            int flags = r.isWhitelist ? 1 : 2;
            idx->empty = false;
            switch (r.mode) {
            case MatchMode::Exact:
                idx->exact[f][r.pattern] |= flags;
                idx->hasExact[f] = true;
                break;
            case MatchMode::Contains:
                idx->contains[f].Add(r.pattern, flags);
                idx->hasContains[f] = true;
                break;
            default:
                idx->wilds.push_back(r);
                idx->hasWild[f] = true;
                break;
            }
        }
        for (int f = 0; f < 4; ++f) if (idx->hasContains[f]) idx->contains[f].Build();
        return idx;
    }

    inline std::shared_ptr<const RuleIndex> RulesIndexView;

    inline std::atomic<bool> Running{ false };
    // 开关状态变更回调：MainWindow 常驻注册（负责"设置 -> 引擎"同步），
    // PopupBlockerPage 显示期间临时注册以刷新开关 UI，析构时只摘除自己的那份。
    inline owner_function<void()> EnabledChangedCallback;

    // 拦截事件通知回调（Toast）：MainWindow 层注册，生命周期与主窗口一致；
    // 写入/清空必须持有 CallbackMutex，消费方经 SafeInvoke 持锁拷贝后无锁调用。
    inline std::function<void(std::wstring const& exeName, std::wstring const& windowTitle, int matchResult)> BlockOccurredCallback;

    inline bool ForceBlock = false;
    inline std::wstring SelfExe;
    inline bool ToastNotify = true;

    inline constexpr int kMLArbLow = 35;
    inline constexpr int kMLArbHigh = 90;

    inline std::atomic<bool> Paused{ false };
    inline std::atomic<bool> ShuttingDown{ false };
    inline std::atomic<long long> PauseDeadlineMs{ 0 };
    inline std::atomic<int> PauseGen{ 0 };

    inline int HeuristicMode = 0;
    inline int HeuristicThreshold = 70;
    inline bool VerboseLog = false;
    inline bool MLHeuristic = false;

    inline long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline std::wstring LogPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        p = p.substr(0, pos + 1) + L"blocklog.txt";
        return p;
    }

    inline bool WildcardMatch(const wchar_t* str, const wchar_t* pat) {
        const wchar_t* s = str, * p = pat;
        const wchar_t* star_s = nullptr, * star_p = nullptr;
        while (*s) {
            if (*p == L'?' || ::towlower(*p) == ::towlower(*s)) { s++; p++; }
            else if (*p == L'*') { star_p = p++; star_s = s; }
            else if (star_p) { p = star_p + 1; s = ++star_s; }
            else return false;
        }
        while (*p == L'*') p++;
        return *p == L'\0';
    }

    inline void InitSelfExe()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        SelfExe = Lower((pos == std::wstring::npos) ? p : p.substr(pos + 1));
    }

    inline bool LooksLikePopup(HWND hwnd)
    {
        LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
        LONG ex = ::GetWindowLongW(hwnd, GWL_EXSTYLE);

        if (::GetWindow(hwnd, GW_OWNER)) return true;
        if (ex & WS_EX_TOOLWINDOW) return true;

        bool resizable = (style & WS_THICKFRAME) != 0;
        bool hasMinMax = (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) != 0;
        if (!resizable && !hasMinMax) return true;

        return false;
    }

    inline void SaveRules(std::vector<Rule> const& newRules)
    {
        auto idx = BuildRuleIndex(newRules); // 锁外构建索引
        std::lock_guard lock(RulesMutex);//不得在持有 RulesMutex 时调用 SaveRules
        SaveRulesJson(newRules, CommunityRemoved);
        Rules = newRules;
        RulesView = std::make_shared<const std::vector<Rule>>(newRules);
        RulesIndexView = idx;
    }

    inline bool AddWhitelistExe(std::wstring const& exe)
    {
        Rule r;
        r.isWhitelist = true;
        r.field = RuleField::Exe;
        r.mode = MatchMode::Exact;
        r.pattern = Lower(exe);

        std::vector<Rule> rules;
        { std::lock_guard lock(RulesMutex); rules = Rules; }

        std::wstring k = RuleKey(r);
        if (std::any_of(rules.begin(), rules.end(),
            [&](Rule const& e) { return RuleKey(e) == k; })) return false;

        rules.push_back(r);
        SaveRules(rules);
        return true;
    }

    inline void SyncFromSettings()
    {
        ForceBlock = AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1;
        HeuristicMode = AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0);
        HeuristicThreshold = AppSettings::ReadInt(L"Blocker", L"HeuristicThreshold", 70);
        VerboseLog = AppSettings::ReadInt(L"Blocker", L"VerboseLog", 0) == 1;
        MLHeuristic = AppSettings::ReadInt(L"Blocker", L"MLHeuristic", 0) == 1;
        ToastNotify = AppSettings::ReadInt(L"Blocker", L"ToastNotify", 1) == 1;

        EnsureDefaultRules();
        std::vector<Rule> rules;
        std::vector<std::wstring> removed;
        LoadRulesJson(rules, removed);
        auto idx = BuildRuleIndex(rules); // 锁外构建索引
        std::lock_guard lock(RulesMutex);
        Rules = std::move(rules);
        CommunityRemoved = std::move(removed);
        RulesView = std::make_shared<const std::vector<Rule>>(Rules);
        RulesIndexView = idx;
    }

    inline std::wstring Sha256Hex(winrt::Windows::Storage::Streams::IBuffer const& buf)
    {
        using namespace winrt::Windows::Security::Cryptography;
        using namespace winrt::Windows::Security::Cryptography::Core;
        auto provider = HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Sha256());
        auto hashed = provider.HashData(buf);
        return Lower(std::wstring(CryptographicBuffer::EncodeToHexString(hashed)));
    }

    inline std::wstring ParseExpectedSha(std::string const& text)
    {
        std::wstring w = Utf8ToWString(text);
        auto pos = w.find(L':');
        std::wstring hex = (pos != std::wstring::npos) ? w.substr(pos + 1) : w;
        size_t b = hex.find_first_not_of(L" \t\r\n");
        size_t e = hex.find_last_not_of(L" \t\r\n");
        if (b == std::wstring::npos) return {};
        return Lower(hex.substr(b, e - b + 1));
    }

    inline winrt::Windows::Foundation::IAsyncAction FetchCommunityRulesAsync()
    {
        using namespace winrt::Windows::Web::Http;
        bool ok = false;
        std::wstring msg;
        std::string body;
        bool verified = false;
        try {
            HttpClient client;
            std::wstring base = L"https://raw.githubusercontent.com/lmg325586/PopKiller/master/";
            std::wstring tick = L"?t=" + std::to_wstring(::GetTickCount64());

            HttpResponseMessage shaResp = co_await client.GetAsync(
                winrt::Windows::Foundation::Uri(base + L"community_rules_sha256" + tick));
            if (shaResp.StatusCode() == winrt::Windows::Web::Http::HttpStatusCode::Ok) {
                std::string shaText = winrt::to_string(co_await shaResp.Content().ReadAsStringAsync());
                std::wstring expected = ParseExpectedSha(shaText);
                if (expected.size() == 64) {
                    HttpResponseMessage resp = co_await client.GetAsync(
                        winrt::Windows::Foundation::Uri(base + L"community_rules.json" + tick));
                    if (resp.StatusCode() == winrt::Windows::Web::Http::HttpStatusCode::Ok) {
                        auto buf = co_await resp.Content().ReadAsBufferAsync();
                        body.assign(reinterpret_cast<char const*>(buf.data()), buf.Length());
                        if (Sha256Hex(buf) == expected) verified = true;
                        else msg = L"SHA256 校验失败";
                    }
                    else msg = L"HTTP " + std::to_wstring(static_cast<int>(resp.StatusCode()));
                }
                else msg = L"校验文件格式错误";
            }
            else msg = L"校验文件 HTTP " + std::to_wstring(static_cast<int>(shaResp.StatusCode()));

            if (verified) {
                std::vector<Rule> fetched;
                if (ParseRulesFromJsonString(body, fetched)) {
                    for (auto& r : fetched) r.fromCommunity = true;

                    std::vector<Rule> merged;
                    std::vector<std::wstring> removed;
                    {
                        std::lock_guard lock(RulesMutex);
                        merged = Rules;
                        removed = CommunityRemoved;
                    }

                    size_t added = 0;
                    for (auto& cr : fetched) {
                        std::wstring k = RuleKey(cr);
                        bool gone = std::find(removed.begin(), removed.end(), k) != removed.end();
                        bool exists = std::any_of(merged.begin(), merged.end(),
                            [&](Rule const& r) { return RuleKey(r) == k; });
                        if (!gone && !exists) { merged.push_back(cr); ++added; }
                    }

                    if (added > 0) SaveRules(merged);
                    ok = true;
                    msg = std::to_wstring(added);
                }
                else {
                    msg = L"JSON 解析失败";
                }
            }
        }
        catch (...) {
            msg = L"网络错误";
        }

        if (ShuttingDown.load()) co_return;

        SafeInvoke(CommunityRulesFetchCallback, ok, msg);
    }

    inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    namespace detail
    {
        inline std::thread Worker;
        inline HWINEVENTHOOK HookShow{};
        inline HWINEVENTHOOK HookFg{};

        inline std::wstring GetProcessName(HWND hwnd)
        {
            DWORD pid{};
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return {};
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return {};
            WCHAR path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            std::wstring name;
            if (::QueryFullProcessImageNameW(h, 0, path, &size))
            {
                std::wstring p = path;
                auto pos = p.find_last_of(L"\\/");
                name = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
            }
            ::CloseHandle(h);
            return Lower(name);
        }

        inline std::wstring GetProcessPath(HWND hwnd)
        {
            DWORD pid{};
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return {};
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return {};
            WCHAR path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            std::wstring result;
            if (::QueryFullProcessImageNameW(h, 0, path, &size)) result = path;
            ::CloseHandle(h);
            return Lower(result);
        }

        inline bool IsProtected(HWND hwnd)
        {
            std::wstring exe = GetProcessName(hwnd);
            if (!SelfExe.empty() && exe == SelfExe) return true;

            static const wchar_t* list[] = {
                // 外壳与桌面
                L"explorer.exe", L"dwm.exe", L"sihost.exe",
                L"shellexperiencehost.exe", L"startmenuexperiencehost.exe",
                L"searchui.exe", L"searchhost.exe", L"searchapp.exe",
                L"lockapp.exe", L"applicationframehost.exe", L"backgroundtaskhost.exe",
                L"runtimebroker.exe", L"credentialuibroker.exe", L"consent.exe",
                L"peopleexperiencehost.exe",
                // 登录与安全
                L"winlogon.exe", L"logonui.exe", L"smartscreen.exe", L"securityhealthsystray.exe",
                // 输入法与辅助功能
                L"ctfmon.exe", L"textinputhost.exe", L"tabtip.exe", L"osk.exe",
                L"narrator.exe", L"magnify.exe", L"sethc.exe", L"utilman.exe",
                // 系统工具与对话框
                L"taskmgr.exe", L"systemsettings.exe", L"systemsettingsbroker.exe",
                L"control.exe", L"mmc.exe", L"openwith.exe", L"msiexec.exe",
                L"sndvol.exe", L"snippingtool.exe", L"screensketch.exe",
                L"mstsc.exe", L"conhost.exe", L"shellhost.exe", L"snippingtool.exe",
                L"screensketch.exe", L"mspaint.exe", L"calc.exe",L"svchost.exe", L"services.exe", L"lsass.exe", L"csrss.exe",
                // Win11 小组件
                L"widgets.exe", L"widgetservice.exe",
                //常见软件
                L"msedge.exe",L"windowsterminal.exe",L"chrome.exe",L"firefox.exe",
            };
            for (auto p : list) if (exe == p) return true;
            return false;
        }

        inline std::wstring GetTitle(HWND hwnd) { WCHAR buf[256]{}; ::GetWindowTextW(hwnd, buf, 256); return Lower(buf); }
        inline std::wstring GetClass(HWND hwnd) { WCHAR buf[256]{}; ::GetClassNameW(hwnd, buf, 256); return Lower(buf); }

        inline bool MatchRule(HWND hwnd, const Rule& r, std::wstring& exe, std::wstring& path, std::wstring& title, std::wstring& cls) {
            std::wstring target;
            switch (r.field) {
            case RuleField::Exe: if (exe.empty()) exe = GetProcessName(hwnd); target = exe; break;
            case RuleField::Path: if (path.empty()) path = GetProcessPath(hwnd); target = path; break;
            case RuleField::Title: if (title.empty()) title = GetTitle(hwnd); target = title; break;
            case RuleField::Class: if (cls.empty()) cls = GetClass(hwnd); target = cls; break;
            }
            switch (r.mode) {
            case MatchMode::Exact: return target == r.pattern;
            case MatchMode::Contains: return target.find(r.pattern) != std::wstring::npos;
            case MatchMode::Wildcard: return WildcardMatch(target.c_str(), r.pattern.c_str());
            }
            return false;
        }

        // 0=未命中, 1=白名单, 2=黑名单
        inline int Match(HWND hwnd) {
            std::shared_ptr<const RuleIndex> idx;
            { std::lock_guard lock(RulesMutex); idx = RulesIndexView; }
            if (!idx || idx->empty) return 0;

            std::wstring t[4];
            bool loaded[4]{};
            auto target = [&](int f) -> std::wstring const& {
                if (!loaded[f]) {
                    switch (f) {
                    case 0: t[f] = GetProcessName(hwnd); break;
                    case 1: t[f] = GetProcessPath(hwnd); break;
                    case 2: t[f] = GetTitle(hwnd); break;
                    default: t[f] = GetClass(hwnd); break;
                    }
                    loaded[f] = true;
                }
                return t[f];
                };

            int flags = 0;
            for (int f = 0; f < 4 && flags != 3; ++f) {
                if (!idx->hasExact[f] && !idx->hasContains[f] && !idx->hasWild[f]) continue; // 无规则 field：零系统调用
                if (idx->hasExact[f] || idx->hasContains[f]) {
                    std::wstring const& s = target(f);
                    if (idx->hasExact[f]) {
                        auto it = idx->exact[f].find(s);
                        if (it != idx->exact[f].end()) flags |= it->second;
                    }
                    if (idx->hasContains[f]) flags |= idx->contains[f].Scan(s);
                }
            }
            for (auto const& r : idx->wilds) {
                if (flags == 3) break;
                if (WildcardMatch(target(static_cast<int>(r.field)).c_str(), r.pattern.c_str()))
                    flags |= r.isWhitelist ? 1 : 2;
            }
            return (flags & 1) ? 1 : (flags & 2) ? 2 : 0;
        }

        inline void Log(std::wstring const& s)
        {
            std::wstring p = LogPath();
            constexpr long long Limit = 1024 * 1024;

            WIN32_FILE_ATTRIBUTE_DATA fad{};
            bool exists = (::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad) != FALSE);
            long long size = exists
                ? (static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow
                : 0;

            bool fresh = !exists || size > Limit;
            FILE* f{};
            if (_wfopen_s(&f, p.c_str(), fresh ? L"wb" : L"ab") == 0 && f)
            {
                if (fresh) ::fwrite("\xEF\xBB\xBF", 1, 3, f);

                SYSTEMTIME st{}; ::GetLocalTime(&st);
                WCHAR ts[32]{};
                swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                std::wstring full = ts + s;

                int need = ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (need > 0)
                {
                    std::string utf8(static_cast<size_t>(need) - 1, '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, utf8.data(), need, nullptr, nullptr);
                    utf8 += "\r\n";
                    ::fwrite(utf8.data(), 1, utf8.size(), f);
                }
                ::fclose(f);
            }
        }

        struct EventVerdict {
            std::wstring action = L"monitor";
            std::wstring reason;
            std::wstring detail;
            bool shouldBlock = false;
            bool shouldLog = false;
            int  matchResult = 0;
        };

        inline bool PassEventFilter(HWND hwnd, LONG idObject, LONG idChild)
        {
            if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return false;
            if (!::IsWindowVisible(hwnd)) return false;
            if (::GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
            if (IsProtected(hwnd)) return false;
            return true;
        }

        inline EventVerdict EvaluateWindow(HWND hwnd, DWORD idEventTime)
        {
            EventVerdict v;
            bool isPopup = LooksLikePopup(hwnd);
            v.matchResult = Match(hwnd);

            if (v.matchResult == 1) {
                v.reason = L"whitelist";
                v.action = L"allow";
            }
            else if (v.matchResult == 2) {
                v.reason = L"blacklist";
                if (ForceBlock || isPopup) { v.shouldBlock = true; v.action = L"block"; }
            }
            else if (HeuristicMode > 0) {

                HeuristicScorer::Features f = HeuristicScorer::ExtractFeatures(hwnd, idEventTime);
                int score = HeuristicScorer::ScoreWindow(f, v.detail);

                v.detail += L" raw=" + HeuristicScorer::BuildRawBits(f);

                bool mlYes = false;
                if (MLHeuristic) {
                    if (score >= kMLArbLow && score <= kMLArbHigh) {
                        mlYes = HeuristicML::GetInstance().Predict(hwnd, idEventTime);
                        v.detail += mlYes ? L" ml=Y" : L" ml=N";
                    }
                    else {
                        v.detail += L" ml=-";
                    }
                }
                v.reason = L"heuristic(" + std::to_wstring(score) + L")";
                if (HeuristicMode == 2) {
                    bool block;
                    if (score > kMLArbHigh) block = true;
                    else if (score < kMLArbLow) block = false;
                    else block = MLHeuristic ? mlYes : (score >= HeuristicThreshold);
                    if (block) { v.shouldBlock = true; v.action = L"block"; }
                }
            }
            else {
                v.reason = L"heuristic_off";
            }

            v.shouldLog = VerboseLog || v.shouldBlock || v.matchResult == 1 || v.matchResult == 2;
            return v;
        }

        inline void WriteEventLog(HWND hwnd, DWORD idEvent, EventVerdict const& v)
        {
            std::wstring logMsg = L"action=" + v.action +
                L" | ev=" + (idEvent == EVENT_OBJECT_SHOW ? L"SHOW" : L"FG") +
                L" | reason=" + v.reason;
            if (!v.detail.empty()) logMsg += L" | " + v.detail;
            logMsg += L" | title=" + GetTitle(hwnd) +
                L" | class=" + GetClass(hwnd) + L" | exe=" + GetProcessName(hwnd);
            Log(logMsg);
        }

        inline void ScheduleForceKill(HWND hwnd)
        {
            std::thread([hwnd]() {
                ::Sleep(400);
                if (!::IsWindow(hwnd)) return;
                DWORD pid = 0;
                ::GetWindowThreadProcessId(hwnd, &pid);
                if (!pid) return;
                HANDLE hProcess = ::OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
                if (!hProcess) return;
                WCHAR path[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                if (::QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    std::wstring p = path;
                    std::transform(p.begin(), p.end(), p.begin(), ::towlower);
                    bool isSystemPath = (p.find(L"c:\\windows\\") == 0) ||
                        (p.find(L"c:\\program files\\") == 0) ||
                        (p.find(L"c:\\program files (x86)\\") == 0);
                    if (!isSystemPath) ::TerminateProcess(hProcess, 0);
                }
                ::CloseHandle(hProcess);
                }).detach();
        }

        inline void EnforceBlock(HWND hwnd, int matchResult)
        {
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ::ShowWindowAsync(hwnd, SW_HIDE);
            if (matchResult == 2) ScheduleForceKill(hwnd);
        }

        inline DWORD WINAPI ThreadMain(LPVOID) {
            HookShow = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            HookFg = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            MSG msg; while (::GetMessageW(&msg, nullptr, 0, 0) > 0) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            if (HookShow) ::UnhookWinEvent(HookShow); if (HookFg) ::UnhookWinEvent(HookFg);
            HookShow = HookFg = nullptr; return 0;
        }
    }

    inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD idEvent, HWND hwnd,
        LONG idObject, LONG idChild, DWORD, DWORD idEventTime)
    {
        if (!detail::PassEventFilter(hwnd, idObject, idChild)) return;
        detail::EventVerdict v = detail::EvaluateWindow(hwnd, idEventTime);
        if (v.shouldLog) detail::WriteEventLog(hwnd, idEvent, v);
        if (v.shouldBlock) {
            detail::EnforceBlock(hwnd, v.matchResult);

            if (!ShuttingDown.load()) {
                SafeInvoke(BlockOccurredCallback, detail::GetProcessName(hwnd), detail::GetTitle(hwnd), v.matchResult);
            }
        }
    }

    inline void Start() { if (Running.exchange(true)) return; InitSelfExe(); detail::Worker = std::thread([] { detail::ThreadMain(nullptr); }); }
    inline void Stop() { if (!Running.exchange(false)) return; ::PostThreadMessageW(::GetThreadId(detail::Worker.native_handle()), WM_QUIT, 0, 0); if (detail::Worker.joinable()) detail::Worker.join(); }

    inline void PauseForMinutes(int minutes)
    {
        bool wasPaused = Paused.exchange(true);
        if (!wasPaused && Running.load()) Stop();

        PauseDeadlineMs.store(NowMs() + static_cast<long long>(minutes) * 60000);
        int gen = ++PauseGen;

        std::thread([gen]() {
            while (Paused.load() && PauseGen.load() == gen && !ShuttingDown.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (NowMs() >= PauseDeadlineMs.load()) {
                    bool expected = true;
                    if (Paused.compare_exchange_strong(expected, false)) {
                        if (!ShuttingDown.load() && AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1) {
                            Start();
                        }
                    }
                    return;
                }
            }
            }).detach();
    }

    inline void ResumeNow()
    {
        if (Paused.exchange(false)) {
            ++PauseGen;
            if (!ShuttingDown.load() && AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1) {
                Start();
            }
        }
    }
}