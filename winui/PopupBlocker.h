#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <queue>
#include <map>
#include <unordered_map>
#include <memory>
#include <utility>
#include "HeuristicML.h"
#include "AppSettings.h"
#include "HeuristicScorer.h"
#include "RuleTypes.h"
#include "RuleStorage.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cstdio>
#include <cwchar>

namespace PopupBlocker
{
    // ===== 常量 =====
    inline constexpr int kFieldCount = 4;                        // RuleIndex 字段数（exe/path/title/class）
    inline constexpr int kFlagWhite = 1;                         // 白名单位
    inline constexpr int kFlagBlack = 2;                         // 黑名单位
    inline constexpr int kFlagBoth = kFlagWhite | kFlagBlack;    // 白+黑
    inline constexpr long long kLogMaxBytes = 1024 * 1024;       // 日志文件大小上限
    inline constexpr size_t kUtf8BomLen = 3;                     // UTF-8 BOM 字节数
    inline constexpr size_t kWildcardMaxLen = 128;               // 通配符模式最大长度
    inline constexpr size_t kWildcardMaxStars = 16;              // 通配符 * 数量上限
    inline constexpr size_t kSha256HexLen = 64;                  // SHA256 十六进制串长度
    inline constexpr UINT kFullscreenPollIntervalMs = 1000;      // 全屏状态轮询间隔
    inline constexpr DWORD kReadyWaitMs = 2000;                  // 等待工作线程就绪超时
    inline constexpr int kMsPerMinute = 60000;                   // 每分钟毫秒数
    inline constexpr unsigned long long kNewlyCreatedWindowMs = 5000;   // 新创建进程判定窗口
    inline constexpr unsigned long long kFiletimeTicksPerMs = 10000ULL; // FILETIME 每毫秒 tick 数
    inline constexpr int kClassNameFallbackBuf = 256;            // 类名兜底缓冲长度
    inline constexpr int kTimestampBufferLen = 32;               // 日志时间戳缓冲长度
    inline constexpr int kHeuristicThresholdDefault = 70;        // 启发式阈值默认值
    inline constexpr int kHeuristicThresholdMin = 0;             // 启发式阈值下限
    inline constexpr int kHeuristicThresholdMax = 100;           // 启发式阈值上限

    inline std::vector<Rule> Rules;
    inline std::vector<std::wstring> CommunityRemoved;
    inline std::function<void(bool, std::wstring)> CommunityRulesFetchCallback;
    inline std::mutex RulesMutex;

    inline std::mutex CallbackMutex;

    template <typename Sig, typename... Args>
    inline void SafeInvoke(std::function<Sig>& slot, Args&&... args)
    {
        std::function<Sig> f;
        { std::lock_guard lock(CallbackMutex); f = slot; }
        if (f) f(std::forward<Args>(args)...);
    }

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
                    if (flags == kFlagBoth) return flags;
                }
            }
            return flags;
        }
    };

    struct RuleIndex
    {
        std::unordered_map<std::wstring, int> exact[kFieldCount];
        AcAutomaton contains[kFieldCount];
        std::vector<Rule> wilds;        // 单条件通配符规则保留遍历
        std::vector<Rule> linear;       // 多条件 / 类名随机规则：事件时线性 AND 扫描
        bool hasExact[kFieldCount]{};
        bool hasContains[kFieldCount]{};
        bool hasWild[kFieldCount]{};
        bool hasLinear = false;
        bool empty = true;
    };

    inline std::shared_ptr<const RuleIndex> BuildRuleIndex(std::vector<Rule> const& rules)
    {
        auto idx = std::make_shared<RuleIndex>();
        for (auto const& r : rules) {
            if (r.conditions.empty()) continue;
            idx->empty = false;

            // 仅「单条件且非类名随机」可进快速索引；其余走线性 AND
            if (r.conditions.size() != 1 || r.conditions[0].mode == MatchMode::RandomClass) {
                idx->linear.push_back(r);
                idx->hasLinear = true;
                continue;
            }

            RuleCondition const& c = r.conditions[0];
            int f = static_cast<int>(c.field);
            int flags = r.isWhitelist ? kFlagWhite : kFlagBlack;
            switch (c.mode) {
            case MatchMode::Exact:
                idx->exact[f][c.pattern] |= flags;
                idx->hasExact[f] = true;
                break;
            case MatchMode::Contains:
                idx->contains[f].Add(c.pattern, flags);
                idx->hasContains[f] = true;
                break;
            default:
                idx->wilds.push_back(r);
                idx->hasWild[f] = true;
                break;
            }
        }
        for (int f = 0; f < kFieldCount; ++f) if (idx->hasContains[f]) idx->contains[f].Build();
        return idx;
    }

    inline std::shared_ptr<const RuleIndex> RulesIndexView;

    inline std::atomic<bool> Running{ false };
    inline std::function<void()> EnabledChangedCallback;
    inline bool ForceBlock = false;
    inline bool GameMode = false;   // 游戏模式：全屏游戏时拦截焦点窃取
    inline std::wstring SelfExeName;
    inline bool ToastNotify = true;

    inline constexpr int kMLArbitrationLow = 35;
    inline constexpr int kMLArbitrationHigh = 95;

    inline std::atomic<bool> Paused{ false };
    inline std::atomic<bool> ShuttingDown{ false };
    inline std::atomic<bool> FullscreenGame{ false };   // 是否处于全屏游戏/全屏应用（轮询缓存）
    inline std::atomic<long long> PauseDeadlineMs{ 0 };
    inline std::atomic<int> PauseGen{ 0 };

    inline int HeuristicMode = 0;
    inline int HeuristicThreshold = kHeuristicThresholdDefault;
    inline bool VerboseLog = false;
    inline bool MLHeuristic = false;

    inline std::function<void(std::wstring const& exeName, std::wstring const& windowTitle, int matchResult)> BlockOccurredCallback;

    inline long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 是否处于全屏游戏/全屏应用：只读由工作线程定时刷新的缓存。
    // 钩子为 WINEVENT_OUTOFCONTEXT，回调送达时前台已切到弹窗，现场查询会读到弹窗状态，
    // 因此必须用“弹窗前”轮询到的缓存值判定。
    inline bool InFullscreenGame()
    {
        return FullscreenGame.load();
    }

    inline std::wstring LogPath()
    {
        std::wstring p = GetSelfPath();
        auto pos = p.find_last_of(L"\\/");
        p = p.substr(0, pos + 1) + L"blocklog.txt";
        return p;
    }

    // 日志内存缓冲：按行累积，达阈值或显式 Flush 才落盘，减少高频拦截时的磁盘抖动。
    // 落盘由独立写线程完成，钩子线程只入缓冲并唤醒，避免前台被磁盘 IO 阻塞。
    inline std::mutex LogMutex;
    inline std::string LogBuffer;
    inline constexpr size_t kLogFlushBytes = 64 * 1024;   // 64KB

    inline std::thread LogWriterThread;
    inline std::condition_variable LogCv;
    inline std::atomic<bool> LogQuit{ false };
    inline bool LogFlushRequested = false;   // 仅在 LogMutex 内访问
    inline bool LogWriting = false;          // 仅在 LogMutex 内访问
    inline std::atomic<bool> LogWriterRunning{ false };

    // 落盘：大小上限/BOM/写入；不对 LogBuffer 加锁，仅由写线程或写线程未运行时的内联回退调用，
    // 保证同一时刻只有一处做 IO。
    inline void WriteLogBytes(std::string const& bytes)
    {
        if (bytes.empty()) return;
        std::wstring p = LogPath();
        constexpr long long Limit = kLogMaxBytes;

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        bool exists = (::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad) != FALSE);
        long long size = exists
            ? ((static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow) : 0;
        bool fresh = !exists || size == 0 || size > Limit;   // 新文件/空文件/超过上限 → 截断重写并写 BOM

        FILE* f{};
        if (_wfopen_s(&f, p.c_str(), fresh ? L"wb" : L"ab") == 0 && f)
        {
            if (fresh) ::fwrite("\xEF\xBB\xBF", 1, kUtf8BomLen, f);
            ::fwrite(bytes.data(), 1, bytes.size(), f);
            ::fclose(f);
        }
        // 写失败：内容已由调用方从 LogBuffer 移出，直接丢弃，避免在写线程内无限重试。
    }

    // 写线程主循环：等待缓冲达阈值或显式 Flush 请求，取走缓冲后无锁落盘。
    inline void LogWriterMain()
    {
        for (;;)
        {
            std::string out;
            {
                std::unique_lock<std::mutex> lk(LogMutex);
                LogCv.wait(lk, [] { return LogQuit || LogFlushRequested || LogBuffer.size() >= kLogFlushBytes; });
                if (LogBuffer.empty() && LogQuit) return;
                out.swap(LogBuffer);
                LogFlushRequested = false;
                LogWriting = true;
            }
            if (!out.empty()) WriteLogBytes(out);
            {
                std::lock_guard<std::mutex> lk(LogMutex);
                LogWriting = false;
            }
            LogCv.notify_all();
        }
    }

    inline void StartLogWriter()
    {
        if (LogWriterRunning.load()) return;
        LogQuit.store(false);
        LogWriterRunning.store(true);
        LogWriterThread = std::thread(LogWriterMain);
    }

    inline void StopLogWriter()
    {
        if (!LogWriterRunning.load()) return;
        { std::lock_guard<std::mutex> lk(LogMutex); LogQuit.store(true); }
        LogCv.notify_all();
        if (LogWriterThread.joinable()) LogWriterThread.join();
        LogWriterRunning.store(false);
    }

    // 供 UI/退出路径调用：请求写线程排空并等待落盘；写线程未运行时内联落盘。
    inline void FlushLog()
    {
        if (!LogWriterRunning)
        {
            std::lock_guard<std::mutex> lk(LogMutex);
            if (!LogBuffer.empty()) { std::string out; out.swap(LogBuffer); WriteLogBytes(out); }
            return;
        }
        { std::lock_guard<std::mutex> lk(LogMutex); LogFlushRequested = true; }
        LogCv.notify_all();
        std::unique_lock<std::mutex> lk(LogMutex);
        LogCv.wait(lk, [] { return LogBuffer.empty() && !LogFlushRequested && !LogWriting; });
    }

    // 清空日志：丢弃缓冲并截断文件（避免清空后旧缓冲又被写回）
    inline void ClearLog()
    {
        {
            std::unique_lock<std::mutex> lk(LogMutex);
            LogBuffer.clear();
            // 写线程若正在写旧内容，先等它写完，否则截断后旧内容会被重新追加回来。
            LogCv.wait(lk, [] { return !LogWriting; });
        }
        FILE* f{};
        if (_wfopen_s(&f, LogPath().c_str(), L"wb") == 0 && f) ::fclose(f);
    }

    inline bool WildcardMatch(const wchar_t* str, const wchar_t* pat) {
        // pattern 来自远端，病态 * 密集会反复回溯：过长或 * 过多时退化为精确比较，避免高开销重复求值
        size_t patLen = 0, starCount = 0;
        for (const wchar_t* q = pat; *q; ++q) { ++patLen; if (*q == L'*') ++starCount; }
        if (patLen > kWildcardMaxLen || starCount > kWildcardMaxStars) return ::wcscmp(str, pat) == 0;

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

    inline void InitSelfExeName()
    {
        std::wstring p = GetSelfPath();
        auto pos = p.find_last_of(L"\\/");
        SelfExeName = Lower((pos == std::wstring::npos) ? p : p.substr(pos + 1));
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

    // 唯一允许改 Rules / CommunityRemoved 的入口：持锁内读-改-写 + 重建索引 + 落盘，
    // 保证 Rules 与 RulesIndexView 原子一致，避免各处锁外「读-改-写」互相覆盖。
    inline void MutateRules(std::function<void(std::vector<Rule>&, std::vector<std::wstring>&)> mutate)
    {
        std::lock_guard<std::mutex> lock(RulesMutex);
        mutate(Rules, CommunityRemoved);
        auto idx = BuildRuleIndex(Rules);
        SaveRulesJson(Rules, CommunityRemoved);
        RulesIndexView = idx;
    }

    inline void SaveRules(std::vector<Rule> const& newRules)
    {
        MutateRules([&](std::vector<Rule>& rules, std::vector<std::wstring>&) { rules = newRules; });
    }

    inline bool AddWhitelistExe(std::wstring const& exe)
    {
        Rule r;
        r.isWhitelist = true;
        RuleCondition c;
        c.field = RuleField::Exe;
        c.mode = MatchMode::Exact;
        c.pattern = Lower(exe);
        r.conditions.push_back(std::move(c));

        std::wstring k = RuleKey(r);
        bool added = false;
        MutateRules([&](std::vector<Rule>& rules, std::vector<std::wstring>&) {
            bool exists = std::any_of(rules.begin(), rules.end(),
                [&](Rule const& e) { return RuleKey(e) == k; });
            if (!exists) { rules.push_back(r); added = true; }
        });
        return added;
    }

    inline void SyncFromSettings()
    {
        ForceBlock = AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1;
        HeuristicMode = AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0);
        HeuristicThreshold = std::clamp(AppSettings::ReadInt(L"Blocker", L"HeuristicThreshold", kHeuristicThresholdDefault), kHeuristicThresholdMin, kHeuristicThresholdMax);
        VerboseLog = AppSettings::ReadInt(L"Blocker", L"VerboseLog", 0) == 1;
        MLHeuristic = AppSettings::ReadInt(L"Blocker", L"MLHeuristic", 0) == 1;
        ToastNotify = AppSettings::ReadInt(L"Blocker", L"ToastNotify", 1) == 1;
        GameMode = AppSettings::ReadInt(L"Blocker", L"GameMode", 0) == 1;

        EnsureDefaultRules();
        std::vector<Rule> rules;
        std::vector<std::wstring> removed;
        LoadRulesJson(rules, removed);
        MutateRules([&](std::vector<Rule>& curRules, std::vector<std::wstring>& curRemoved) {
            curRules = std::move(rules);
            curRemoved = std::move(removed);
        });
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
                if (expected.size() == kSha256HexLen) {
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

                    if (AppSettings::ReadInt(L"Blocker", L"CommunityRulesEnabled", 1) != 1) {
                        // 拉取期间开关被关闭：视为关闭，不合并
                        ok = true;
                        msg = L"0";
                    }
                    else {
                        size_t added = 0;
                        MutateRules([&](std::vector<Rule>& rules, std::vector<std::wstring>& removed) {
                            for (auto const& cr : fetched) {
                                std::wstring k = RuleKey(cr);
                                bool gone = std::find(removed.begin(), removed.end(), k) != removed.end();
                                bool exists = std::any_of(rules.begin(), rules.end(),
                                    [&](Rule const& r) { return RuleKey(r) == k; });
                                if (!gone && !exists) { rules.push_back(cr); ++added; }
                            }
                        });
                        ok = true;
                        msg = std::to_wstring(added);
                    }
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
        inline std::mutex WorkerMutex;   // 串行化 Start/Stop，避免 Running 与 Worker 状态错配
        inline HANDLE ReadyEvent{};      // 手动重置事件：本线程消息队列已建立
        inline std::atomic<DWORD> LastForegroundPid{ 0 };   // 上一个前台窗口的 pid（“同进程”特征用）

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
            if (!SelfExeName.empty() && exe == SelfExeName) return true;

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
                L"mstsc.exe", L"conhost.exe", L"shellhost.exe",
                L"mspaint.exe", L"calc.exe",L"svchost.exe", L"services.exe", L"lsass.exe", L"csrss.exe",
                // Win11 小组件
                L"widgets.exe", L"widgetservice.exe",
                // 常见软件
                L"msedge.exe",L"windowsterminal.exe",L"chrome.exe",L"firefox.exe",
            };
            for (auto p : list) if (exe == p) return true;
            return false;
        }

        inline std::wstring GetTitle(HWND hwnd)
        {
            int len = ::GetWindowTextLengthW(hwnd);
            if (len <= 0) return {};
            std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
            int written = ::GetWindowTextW(hwnd, buf.data(), len + 1);
            if (written <= 0) return {};
            buf.resize(static_cast<size_t>(written));
            return Lower(buf);
        }
        inline std::wstring GetClass(HWND hwnd)
        {
            int len = ::GetClassNameW(hwnd, nullptr, 0);   // 两遍法：先取所需长度
            if (len > 0) {
                std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
                int written = ::GetClassNameW(hwnd, buf.data(), len + 1);
                if (written > 0) { buf.resize(static_cast<size_t>(written)); return Lower(buf); }
            }
            WCHAR stack[kClassNameFallbackBuf]{};   // 兜底：长度查询失败时退回固定缓冲
            int written = ::GetClassNameW(hwnd, stack, kClassNameFallbackBuf);
            if (written <= 0) return {};
            return Lower(std::wstring(stack, static_cast<size_t>(written)));
        }

        // 单条件求值：target 为该条件字段的取值
        inline bool EvalCondition(RuleCondition const& c, std::wstring const& target)
        {
            switch (c.mode) {
            case MatchMode::RandomClass: return HeuristicScorer::LooksLikeRandomClass(target);
            case MatchMode::Exact:       return target == c.pattern;
            case MatchMode::Contains:    return target.find(c.pattern) != std::wstring::npos;
            case MatchMode::Wildcard:    return WildcardMatch(target.c_str(), c.pattern.c_str());
            }
            return false;
        }

        // 0=未命中, 1=白名单, 2=黑名单
        inline int Match(HWND hwnd) {
            std::shared_ptr<const RuleIndex> idx;
            { std::lock_guard lock(RulesMutex); idx = RulesIndexView; }
            if (!idx || idx->empty) return 0;

            std::wstring t[kFieldCount];
            bool loaded[kFieldCount]{};
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
            for (int f = 0; f < kFieldCount && flags != kFlagBoth; ++f) {
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
                if (flags == kFlagBoth) break;
                RuleCondition const& c = r.conditions[0];
                if (WildcardMatch(target(static_cast<int>(c.field)).c_str(), c.pattern.c_str()))
                    flags |= r.isWhitelist ? kFlagWhite : kFlagBlack;
            }
            if (idx->hasLinear) {
                for (auto const& r : idx->linear) {
                    if (flags == kFlagBoth) break;
                    bool all = true;
                    for (auto const& c : r.conditions) {
                        if (!EvalCondition(c, target(static_cast<int>(c.field)))) { all = false; break; }
                    }
                    if (all) flags |= r.isWhitelist ? kFlagWhite : kFlagBlack;
                }
            }
            return (flags & kFlagWhite) ? kFlagWhite : (flags & kFlagBlack) ? kFlagBlack : 0;
        }

        inline void Log(std::wstring const& s)
        {
            SYSTEMTIME st{}; ::GetLocalTime(&st);
            WCHAR ts[kTimestampBufferLen]{};
            swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring full = ts + s;

            int need = ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (need <= 0) return;

            std::lock_guard<std::mutex> lock(LogMutex);
            size_t old = LogBuffer.size();
            LogBuffer.resize(old + static_cast<size_t>(need) - 1);
            ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, LogBuffer.data() + old, need, nullptr, nullptr);
            LogBuffer += "\r\n";
            if (LogBuffer.size() >= kLogFlushBytes)
            {
                if (LogWriterRunning)
                {
                    LogFlushRequested = true;
                    LogCv.notify_all();
                }
                else
                {
                    // 写线程未运行（引擎未 Start）：内联落盘，保证不丢日志。
                    std::string out; out.swap(LogBuffer);
                    WriteLogBytes(out);
                }
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

        inline EventVerdict EvaluateWindow(HWND hwnd, DWORD idEventTime, DWORD prevForegroundPid)
        {
            EventVerdict v;
            bool isPopup = LooksLikePopup(hwnd);
            v.matchResult = Match(hwnd);

            if (v.matchResult == kFlagWhite) {
                v.reason = L"whitelist";
                v.action = L"allow";
            }
            else if (v.matchResult == kFlagBlack) {
                v.reason = L"blacklist";
                if (ForceBlock || isPopup) { v.shouldBlock = true; v.action = L"block"; }
            }
            else if (HeuristicMode > 0) {

                HeuristicScorer::Features f = HeuristicScorer::ExtractFeatures(hwnd, idEventTime, prevForegroundPid);
                int score = HeuristicScorer::ScoreWindow(f, v.detail);

                v.detail += L" raw=" + HeuristicScorer::BuildRawBits(f);

                bool mlYes = false;
                if (MLHeuristic) {
                    if (score >= kMLArbitrationLow && score <= kMLArbitrationHigh) {
                        mlYes = HeuristicML::GetInstance().Predict(hwnd, idEventTime, prevForegroundPid);
                        v.detail += mlYes ? L" ml=Y" : L" ml=N";
                    }
                    else {
                        v.detail += L" ml=-";
                    }
                }
                v.reason = L"heuristic(" + std::to_wstring(score) + L")";
                if (HeuristicMode == 2) {
                    bool block;
                    if (score > kMLArbitrationHigh) block = true;
                    else if (score < kMLArbitrationLow) block = false;
                    else block = MLHeuristic ? mlYes : (score >= HeuristicThreshold);
                    if (block) { v.shouldBlock = true; v.action = L"block"; }
                }
            }
            else {
                v.reason = L"heuristic_off";
            }

            v.shouldLog = VerboseLog || v.shouldBlock || v.matchResult == kFlagWhite || v.matchResult == kFlagBlack;
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

        inline void EnforceBlock(HWND hwnd)
        {
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ::ShowWindowAsync(hwnd, SW_HIDE);
        }

        // 目标窗口进程是否刚创建（默认 5 秒内）
        inline bool IsNewlyCreated(HWND hwnd, unsigned long long withinMs = kNewlyCreatedWindowMs)
        {
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return false;
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return false;
            FILETIME creation{}, exitT{}, kernel{}, user{};
            BOOL ok = ::GetProcessTimes(h, &creation, &exitT, &kernel, &user);
            ::CloseHandle(h);
            if (!ok) return false;

            FILETIME nowFt{};
            ::GetSystemTimeAsFileTime(&nowFt);
            ULARGE_INTEGER now{}, cre{};
            now.LowPart = nowFt.dwLowDateTime; now.HighPart = nowFt.dwHighDateTime;
            cre.LowPart = creation.dwLowDateTime; cre.HighPart = creation.dwHighDateTime;
            if (now.QuadPart <= cre.QuadPart) return false;
            return ((now.QuadPart - cre.QuadPart) / kFiletimeTicksPerMs) <= withinMs;
        }

        // 焦点窃取：阻止置顶 + 关闭并隐藏
        inline void EnforceFocusSteal(HWND hwnd)
        {
            ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ::ShowWindowAsync(hwnd, SW_HIDE);
        }

        inline EventVerdict MakeFocusStealVerdict()
        {
            EventVerdict v;
            v.reason = L"focus_steal";
            v.action = L"block";
            v.shouldBlock = true;
            v.shouldLog = true;
            return v;
        }

        inline HWND LastFullscreenHwnd = nullptr;   // 抢前台前记录的全屏前台窗口（用于事后把前台还回去）

        // 把前台还给游戏（若仍在且已不是前台）：附加输入队列以绕过前台锁
        inline void RestoreFullscreenForeground()
        {
            HWND game = LastFullscreenHwnd;
            if (!game || !::IsWindow(game)) return;

            HWND fg = ::GetForegroundWindow();
            if (fg == game) return;

            DWORD fgTid = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
            DWORD ourTid = ::GetCurrentThreadId();
            bool attached = false;
            if (fgTid && fgTid != ourTid)
                attached = ::AttachThreadInput(ourTid, fgTid, TRUE) != 0;
            ::SetForegroundWindow(game);
            if (attached) ::AttachThreadInput(ourTid, fgTid, FALSE);
        }

        // 刷新全屏状态缓存：独占全屏(3)、无边框全屏/演示(2)、Store 应用(7) 均视为全屏。
        // out-of-context 钩子回调在“前台已切换”后才送达，故必须由轮询持续采样；
        // 同时记录当时的全屏前台窗口，供拦截后恢复前台。
        inline void UpdateFullscreenState()
        {
            QUERY_USER_NOTIFICATION_STATE q{};
            bool fs = SUCCEEDED(::SHQueryUserNotificationState(&q))
                && (q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_BUSY || q == QUNS_APP);
            FullscreenGame.store(fs);

            if (fs) {
                HWND fg = ::GetForegroundWindow();
                if (fg && fg != ::GetDesktopWindow() && fg != ::GetShellWindow()) {
                    DWORD pid = 0;
                    ::GetWindowThreadProcessId(fg, &pid);
                    if (pid && pid != ::GetCurrentProcessId())
                        LastFullscreenHwnd = fg;
                }
            }
        }

        inline DWORD WINAPI ThreadMain(LPVOID) {
            // 先强制建立本线程消息队列，再通知就绪：保证 Stop() 的
            // PostThreadMessageW(WM_QUIT) 不会因队列尚未建立而静默丢失。
            MSG msg;
            ::PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            if (ReadyEvent) ::SetEvent(ReadyEvent);

            HookShow = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            HookFg = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

            // 约每秒刷新一次全屏缓存（无窗口定时器，WM_TIMER 投递到本线程消息队列）
            UINT_PTR fsTimer = ::SetTimer(nullptr, 0, kFullscreenPollIntervalMs, nullptr);
            UpdateFullscreenState();

            while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (msg.message == WM_TIMER && msg.wParam == fsTimer) { UpdateFullscreenState(); continue; }
                ::TranslateMessage(&msg); ::DispatchMessageW(&msg);
            }
            if (fsTimer) ::KillTimer(nullptr, fsTimer);
            if (HookShow) ::UnhookWinEvent(HookShow); if (HookFg) ::UnhookWinEvent(HookFg);
            HookShow = HookFg = nullptr; return 0;
        }
    }

    inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD idEvent, HWND hwnd,
        LONG idObject, LONG idChild, DWORD, DWORD idEventTime)
    {
        if (ShuttingDown.load()) return;   // 退出链路早退守卫：关闭中不再评估/拦截
        if (!detail::PassEventFilter(hwnd, idObject, idChild)) return;

        // 记录/取用“上一个前台进程”：用于“弹窗是否与刚在前台的进程同进程”特征
        DWORD thisPid = 0; ::GetWindowThreadProcessId(hwnd, &thisPid);
        DWORD prevFgPid = detail::LastForegroundPid.load();
        if (idEvent == EVENT_SYSTEM_FOREGROUND) detail::LastForegroundPid.store(thisPid);

        // 全屏游戏期间：仅当拦截引擎运行时，刚创建、非白名单的进程抢夺前台/新建可见顶层窗
        // → 焦点窃取，直接拦截（静默，不弹通知）。SHOW 分支抢在激活前隐藏，降低游戏退出全屏概率；
        // 拦截后尝试把前台还给游戏。
        if (Running.load()
            && GameMode
            && InFullscreenGame()
            && (idEvent == EVENT_SYSTEM_FOREGROUND || idEvent == EVENT_OBJECT_SHOW)
            && detail::Match(hwnd) != kFlagWhite
            && detail::IsNewlyCreated(hwnd)
            && hwnd != detail::LastFullscreenHwnd)
        {
            detail::EventVerdict fs = detail::MakeFocusStealVerdict();
            detail::WriteEventLog(hwnd, idEvent, fs);
            detail::EnforceFocusSteal(hwnd);
            detail::RestoreFullscreenForeground();
            return;
        }

        detail::EventVerdict v = detail::EvaluateWindow(hwnd, idEventTime, prevFgPid);
        if (v.shouldLog) detail::WriteEventLog(hwnd, idEvent, v);
        if (v.shouldBlock) {
            detail::EnforceBlock(hwnd);

            if (!ShuttingDown.load()) {
                SafeInvoke(BlockOccurredCallback, detail::GetProcessName(hwnd), detail::GetTitle(hwnd), v.matchResult);
            }
        }
    }

    inline void Start()
    {
        std::lock_guard lock(detail::WorkerMutex);
        if (Running.exchange(true)) return;
        InitSelfExeName();
        if (!detail::ReadyEvent)
            detail::ReadyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (detail::ReadyEvent) ::ResetEvent(detail::ReadyEvent);
        detail::Worker = std::thread([] { detail::ThreadMain(nullptr); });
        StartLogWriter();
        // 等消息队列就绪（正常瞬时返回），确保随后的 Stop() 能可靠投递 WM_QUIT。
        if (detail::ReadyEvent) ::WaitForSingleObject(detail::ReadyEvent, kReadyWaitMs);
    }

    // 开启拦截的标准流程：刷新配置缓存 → 惰性加载 ML → 启动引擎
    inline void StartEngine()
    {
        SyncFromSettings();
        HeuristicML::GetInstance().Init();
        Start();
    }

    inline void Stop()
    {
        std::lock_guard lock(detail::WorkerMutex);
        if (!Running.exchange(false)) return;
        if (detail::Worker.joinable())
        {
            ::PostThreadMessageW(::GetThreadId(detail::Worker.native_handle()), WM_QUIT, 0, 0);
            detail::Worker.join();
        }
        StopLogWriter();
    }

    inline void PauseForMinutes(int minutes)
    {
        bool wasPaused = Paused.exchange(true);
        if (!wasPaused && Running.load()) Stop();

        PauseDeadlineMs.store(NowMs() + static_cast<long long>(minutes) * kMsPerMinute);
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