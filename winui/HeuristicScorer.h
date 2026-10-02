#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <utility>
#include <wintrust.h>
#include <tlhelp32.h>

#pragma comment(lib, "wintrust.lib")

namespace HeuristicScorer
{
    // ===== 常量 =====
    inline constexpr float kSmallW = 400.f;                     // 小窗宽阈值（逻辑像素）
    inline constexpr float kSmallH = 300.f;                     // 小窗高阈值（逻辑像素）
    inline constexpr float kLargeW = 800.f;                     // 大窗宽阈值（逻辑像素）
    inline constexpr float kLargeH = 600.f;                     // 大窗高阈值（逻辑像素）
    inline constexpr float kHexRatioThreshold = 0.8f;           // 类名十六进制占比阈值
    inline constexpr float kDigitRatioThreshold = 0.6f;         // 类名数字占比阈值
    inline constexpr size_t kRandomClassMinLen = 6;             // 随机类名最短长度
    inline constexpr float kYoungProcessSec = 120.f;            // 年轻进程秒数阈值
    inline constexpr long long kUserIdleThresholdMs = 5000;     // 视为空闲的毫秒阈值
    inline constexpr float kFarFromMouseDip = 300.f;            // 距鼠标过远的 DIP 阈值
    inline constexpr unsigned long long kProcTableTtlMs = 2000; // 进程表缓存 TTL
    inline constexpr size_t kParentCacheMax = 4096;             // 父进程缓存上限

    namespace detail
    {
        inline std::wstring Lower(std::wstring s)
        {
            std::transform(s.begin(), s.end(), s.begin(), ::towlower);
            return s;
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

        inline std::wstring GetTitle(HWND hwnd)
        {
            WCHAR buf[256]{};
            ::GetWindowTextW(hwnd, buf, 256);
            return Lower(buf);
        }

        inline std::wstring GetClass(HWND hwnd)
        {
            WCHAR buf[256]{};
            ::GetClassNameW(hwnd, buf, 256);
            return Lower(buf);
        }

        inline float IsUserIdle(DWORD evTime)
        {
            LASTINPUTINFO lii{}; lii.cbSize = sizeof(lii);
            if (!::GetLastInputInfo(&lii)) return 0.f;

            LONG diff = static_cast<LONG>(evTime) - static_cast<LONG>(lii.dwTime);
            long long idleMs = diff;
            if (idleMs < 0) idleMs = 0;

            return (idleMs > kUserIdleThresholdMs) ? 1.f : 0.f;
        }

        // scale = DPI/96：阈值按 300 逻辑像素换算成物理像素（300*scale）
        inline float IsFarFromMouse(RECT const& rc, float scale)
        {
            POINT cpt{}; ::GetCursorPos(&cpt);
            int dx = (cpt.x < rc.left) ? (rc.left - cpt.x) : (cpt.x > rc.right ? cpt.x - rc.right : 0);
            int dy = (cpt.y < rc.top) ? (rc.top - cpt.y) : (cpt.y > rc.bottom ? cpt.y - rc.bottom : 0);
            long long d2 = (long long)dx * dx + (long long)dy * dy;
            long long lim = (long long)(kFarFromMouseDip * scale);
            return (d2 > lim * lim) ? 1.f : 0.f;
        }

        // ---- 父进程（启动者）查询：进程表短 TTL 缓存 + 父 exe 名按子进程创建时间缓存 ----
        inline std::wstring ProcessNameByPid(DWORD pid)
        {
            if (!pid) return {};
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return {};
            WCHAR buf[MAX_PATH]{}; DWORD size = MAX_PATH;
            std::wstring name;
            if (::QueryFullProcessImageNameW(h, 0, buf, &size)) {
                std::wstring p = buf;
                auto pos = p.find_last_of(L"\\/");
                name = Lower((pos == std::wstring::npos) ? p : p.substr(pos + 1));
            }
            ::CloseHandle(h);
            return name;
        }

        struct ProcTable {
            std::mutex mtx;
            std::unordered_map<DWORD, DWORD> ppid;   // pid -> 父 pid
            long long builtMs = 0;
        };
        inline ProcTable& ProcTableInstance()
        {
            static ProcTable* t = new ProcTable();
            return *t;
        }

        inline void RebuildProcTable(ProcTable& t)   // 调用方持 t.mtx
        {
            std::unordered_map<DWORD, DWORD> m;
            HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
                if (::Process32FirstW(snap, &pe)) {
                    do { m[pe.th32ProcessID] = pe.th32ParentProcessID; } while (::Process32NextW(snap, &pe));
                }
                ::CloseHandle(snap);
            }
            t.ppid.swap(m);
            t.builtMs = ::GetTickCount64();
        }

        inline unsigned long long ProcCreateTime(DWORD pid)
        {
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return 0;
            FILETIME c{}, e{}, k{}, u{};
            unsigned long long r = 0;
            if (::GetProcessTimes(h, &c, &e, &k, &u)) {
                ULARGE_INTEGER v; v.LowPart = c.dwLowDateTime; v.HighPart = c.dwHighDateTime; r = v.QuadPart;
            }
            ::CloseHandle(h);
            return r;
        }

        struct ParentCache {
            std::mutex mtx;
            std::unordered_map<DWORD, std::pair<unsigned long long, std::wstring>> map; // childPid -> {createTime, parentExe}
        };
        inline ParentCache& ParentCacheInstance()
        {
            static ParentCache* c = new ParentCache();
            return *c;
        }

        // 返回窗口所在进程的“父进程 exe 名”（小写）；失败返回空
        inline std::wstring GetParentExe(DWORD pid)
        {
            if (!pid) return {};
            unsigned long long ct = ProcCreateTime(pid);
            ParentCache& pc = ParentCacheInstance();
            {
                std::lock_guard<std::mutex> l(pc.mtx);
                auto it = pc.map.find(pid);
                if (it != pc.map.end() && it->second.first == ct) return it->second.second;
            }
            DWORD ppid = 0;
            ProcTable& pt = ProcTableInstance();
            {
                std::lock_guard<std::mutex> l(pt.mtx);
                if (::GetTickCount64() - pt.builtMs > kProcTableTtlMs) RebuildProcTable(pt);
                auto it = pt.ppid.find(pid);
                if (it != pt.ppid.end()) ppid = it->second;
            }
            std::wstring parent = ProcessNameByPid(ppid);
            {
                std::lock_guard<std::mutex> l(pc.mtx);
                if (pc.map.size() > kParentCacheMax) pc.map.clear();
                pc.map[pid] = { ct, parent };
            }
            return parent;
        }
    }

    // ===== 权重表 =====
    struct Weights
    {
        float owner = 16;
        float toolWin = 13;
        float topmost = 22;
        float noActivate = 5;
        float notResizable = 13;
        float resizable = -15;
        float noMinMax = 13;
        float hasMinMax = -5;
        float captionSysmenu = -10;
        float smallWindow = 29;
        float largeWindow = -20;
        float titleEmpty = 22;
        float titleKwHit = 48;
        float clsHex = 12;
        float pathTemp = 22;
        float pathRoaming = 14;
        float youngProcess = 6;
        float unsignedExe = 14;
        float unsignedUserDir = 27;
        float signedExe = -5;
        float userIdle = 16;
        float farFromMouse = 11;
        float mouseClose = -25;
        float parentExplorer = -8;              // 父进程是 explorer（外壳/用户启动）→ 更像正常窗
        float parentSystem = 11;                // 父进程是系统/后台宿主 → 可疑
        float parentUnknown = 7;                // 取不到父进程 → 轻微可疑
        float sameProcAsPrevForeground = -12;   // 与“刚在前台的进程”同进程 → 很可能是自家弹窗
    };
    inline Weights g_weights{};

    struct Features
    {
        float hasOwner, toolWin, topmost, noActivate;
        float resizable, hasMinMax, captionSysmenu;
        float wDip, hDip;      // 逻辑像素宽高（已按窗口 DPI 归一）
        float dpiScale;        // 窗口 DPI / 96
        float titleEmpty, titleKwHits;
        float clsHexRatio;
        float pathTemp, pathRoaming;
        float procAgeSec;
        float userIdle;
        float farFromMouse;      // 距鼠标是否超过 300 逻辑像素（按窗口 DPI 归一）
        float parentExplorer, parentSystem, parentUnknown;   // 父进程（启动者）类别
        float sameProcAsPrevForeground;                       // 弹窗进程 == 本次事件前的前台进程
        std::wstring path;
        std::wstring cls;
    };

    inline float DigitRatio(std::wstring const& s)
    {
        if (s.empty()) return 0.f;
        int d = 0; for (wchar_t c : s) if (c >= L'0' && c <= L'9') ++d;
        return float(d) / float(s.size());
    }

    inline float HexRatio(std::wstring const& s)
    {
        if (s.empty()) return 0.f;
        int h = 0;
        for (wchar_t c : s)
            if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')) ++h;
        return float(h) / float(s.size());
    }

    // 类名是否“看起来随机”：长度≥6 且十六进制/数字占比高（供规则“类名随机”条件使用）
    inline bool LooksLikeRandomClass(std::wstring const& cls)
    {
        if (cls.size() < kRandomClassMinLen) return false;
        return HexRatio(cls) >= kHexRatioThreshold || DigitRatio(cls) >= kDigitRatioThreshold;
    }

    inline float ProcessAgeSeconds(HWND hwnd)
    {
        DWORD pid{}; ::GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return -1.f;
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return -1.f;
        float age = -1.f;
        FILETIME ct, et, kt, ut;
        if (::GetProcessTimes(h, &ct, &et, &kt, &ut))
        {
            FILETIME now; ::GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER a{}, b{};
            a.LowPart = now.dwLowDateTime;  a.HighPart = now.dwHighDateTime;
            b.LowPart = ct.dwLowDateTime;   b.HighPart = ct.dwHighDateTime;
            age = float(a.QuadPart - b.QuadPart) / 1e7f;
        }
        ::CloseHandle(h);
        return age;
    }

    // 纯计算：真正调用 WinVerifyTrust（慢，可能数十毫秒）。只允许在后台线程调用，勿在钩子线程直接调用。
    inline bool VerifyFileSignature(std::wstring const& path)
    {
        if (path.empty()) return false;
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = path.c_str();

        static GUID kVerifyV2 =
        { 0x00AAC56B, 0xCD44, 0x11D0, {0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE} };

        WINTRUST_DATA wtd{};
        wtd.cbStruct = sizeof(wtd);
        wtd.dwUIChoice = WTD_UI_NONE;
        wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
        wtd.dwUnionChoice = WTD_CHOICE_FILE;
        wtd.pFile = &file;
        wtd.dwStateAction = WTD_STATEACTION_VERIFY;

        LONG res = ::WinVerifyTrust(nullptr, &kVerifyV2, &wtd);

        wtd.dwStateAction = WTD_STATEACTION_CLOSE;
        ::WinVerifyTrust(nullptr, &kVerifyV2, &wtd);

        return res == ERROR_SUCCESS;
    }

    // ===== 签名验证：后台线程预取 + 缓存 =====
    // 钩子线程绝不阻塞：命中缓存直接返回；未命中只入队并由后台线程验证。
    // 后台线程 detach，随进程退出被回收；下述状态为进程级静态量，不会先于线程销毁。

    // 状态堆分配（进程生命周期，故意不释放）：后台线程 detach 未 join，进程退出的静态析构
    // 会销毁 mutex/cv 造成 UB，故避免使用可析构的命名空间静态量。
    struct SigState {
        std::mutex mtx;
        std::condition_variable cv;
        std::unordered_map<std::wstring, bool> cache;   // path -> 是否已签名
        std::deque<std::wstring> queue;                 // 待验证队列
        std::unordered_set<std::wstring> pending;       // 排队中/验证中，去重
    };
    inline SigState& Sig()
    {
        static SigState* s = new SigState();
        return *s;
    }

    inline void VerifyFileSignature_WorkerMain()
    {
        SigState& st = Sig();
        for (;;)
        {
            std::wstring path;
            {
                std::unique_lock<std::mutex> l(st.mtx);
                st.cv.wait(l, [&] { return !st.queue.empty(); });
                path = std::move(st.queue.front());
                st.queue.pop_front();
            }

            bool s = VerifyFileSignature(path);   // 慢操作，锁外执行

            {
                std::lock_guard<std::mutex> l(st.mtx);
                st.cache[path] = s;
                st.pending.erase(path);
            }
        }
    }

    // 仅入队 + 去重（不加 EnsureSigThread，避免在 call_once 内部重入同一 once_flag）
    inline void EnqueueSignatureCore(std::wstring const& path)
    {
        if (path.empty()) return;
        SigState& st = Sig();
        {
            std::lock_guard<std::mutex> l(st.mtx);
            if (st.cache.find(path) != st.cache.end()) return;
            if (!st.pending.insert(path).second) return;
            st.queue.push_back(path);
        }
        st.cv.notify_one();
    }

    // 预热：把当前所有进程的可执行路径入队，让首次命中就能直接读到结果
    inline void PrimeSignatures()
    {
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (::Process32FirstW(snap, &pe)) {
            do {
                HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (!h) continue;
                WCHAR buf[MAX_PATH]{}; DWORD size = MAX_PATH;
                if (::QueryFullProcessImageNameW(h, 0, buf, &size))
                    EnqueueSignatureCore(detail::Lower(buf));
                ::CloseHandle(h);
            } while (::Process32NextW(snap, &pe));
        }
        ::CloseHandle(snap);
    }

    // 首次调用时启动后台线程（进程生命周期）；预热与验证都在该线程执行，
    // 调用方（钩子线程）绝不被进程枚举或签名校验阻塞。
    inline void EnsureSigThread()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            std::thread([] { PrimeSignatures(); VerifyFileSignature_WorkerMain(); }).detach();
        });
    }

    inline void EnqueueSignature(std::wstring const& path)
    {
        EnsureSigThread();
        EnqueueSignatureCore(path);
    }

    // 永不阻塞：除短暂持锁外不做任何 IO/签名验证。
    // 未命中时乐观返回 true（临时视为“已签名”），后台验证完成后由 SigCache 纠正。
    // 取舍：这样避免首次遇到正常软件就加 unsignedExe / unsignedUserDir 权重（宁可漏、不可误伤），
    // 代价是极短窗口内未签名样本会少拿 unsigned 权重（也可能拿到 signedExe 的 -5）。
    inline bool IsFileSignedCached(std::wstring const& path)
    {
        if (path.empty()) return false;
        SigState& st = Sig();
        {
            std::lock_guard<std::mutex> l(st.mtx);
            auto it = st.cache.find(path);
            if (it != st.cache.end()) return it->second;
        }
        EnqueueSignature(path);
        return true;
    }

    // 采集窗口全部特征：输入 hwnd、事件时间与前前台 pid，输出 Features（DPI 已归一，含类名/路径/父进程）。
    inline Features ExtractFeatures(HWND hwnd, DWORD evTime = 0, DWORD prevForegroundPid = 0)
    {
        if (evTime == 0) evTime = static_cast<DWORD>(::GetTickCount64());

        Features f{};
        LONG st = ::GetWindowLongW(hwnd, GWL_STYLE);
        LONG ex = ::GetWindowLongW(hwnd, GWL_EXSTYLE);
        f.hasOwner = ::GetWindow(hwnd, GW_OWNER) ? 1.f : 0.f;
        f.toolWin = (ex & WS_EX_TOOLWINDOW) ? 1.f : 0.f;
        f.topmost = (ex & WS_EX_TOPMOST) ? 1.f : 0.f;
        f.noActivate = (ex & WS_EX_NOACTIVATE) ? 1.f : 0.f;
        f.resizable = (st & WS_THICKFRAME) ? 1.f : 0.f;
        f.hasMinMax = (st & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ? 1.f : 0.f;
        f.captionSysmenu = ((st & WS_CAPTION) && (st & WS_SYSMENU)) ? 1.f : 0.f;

        RECT rc{}; ::GetWindowRect(hwnd, &rc);
        UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : 0;
        f.dpiScale = (dpi ? float(dpi) : 96.f) / 96.f;
        f.wDip = float(rc.right - rc.left) / f.dpiScale;
        f.hDip = float(rc.bottom - rc.top) / f.dpiScale;

        std::wstring title = detail::GetTitle(hwnd);
        f.titleEmpty = title.empty() ? 1.f : 0.f;
        float kw = 0.f;
        for (auto k : { L"热点", L"资讯", L"推荐", L"广告", L"优惠", L"领取", L"pop", L"ads" })
            if (title.find(k) != std::wstring::npos) kw += 1.f;
        f.titleKwHits = kw;

        std::wstring cls = detail::GetClass(hwnd);
        f.clsHexRatio = HexRatio(cls);
        f.cls = cls;

        f.path = detail::GetProcessPath(hwnd);
        f.pathTemp = f.path.find(L"\\appdata\\local\\temp\\") != std::wstring::npos ? 1.f : 0.f;
        f.pathRoaming = f.path.find(L"\\appdata\\roaming\\") != std::wstring::npos ? 1.f : 0.f;
        f.procAgeSec = ProcessAgeSeconds(hwnd);

        DWORD pid = 0; ::GetWindowThreadProcessId(hwnd, &pid);
        std::wstring parent = detail::GetParentExe(pid);
        f.parentExplorer = (parent == L"explorer.exe") ? 1.f : 0.f;
        f.parentSystem = (parent == L"services.exe" || parent == L"svchost.exe" || parent == L"wininit.exe" ||
            parent == L"winlogon.exe" || parent == L"lsass.exe" || parent == L"taskhostw.exe" ||
            parent == L"runtimebroker.exe") ? 1.f : 0.f;
        f.parentUnknown = parent.empty() ? 1.f : 0.f;
        f.sameProcAsPrevForeground = (prevForegroundPid != 0 && pid == prevForegroundPid) ? 1.f : 0.f;

        f.userIdle = detail::IsUserIdle(evTime);
        f.farFromMouse = detail::IsFarFromMouse(rc, f.dpiScale);
        return f;
    }

    // 把 Features 压成 21 位 T/F 原始串（顺序同权重量表），用于日志与规则调试。
    inline std::wstring BuildRawBits(Features const& f)
    {
        std::wstring b;
        b += (f.hasOwner > 0) ? L'T' : L'F';
        b += (f.toolWin > 0) ? L'T' : L'F';
        b += (f.topmost > 0) ? L'T' : L'F';
        b += (f.noActivate > 0) ? L'T' : L'F';
        b += (f.resizable > 0) ? L'T' : L'F';
        b += (f.hasMinMax > 0) ? L'T' : L'F';
        b += (f.captionSysmenu > 0) ? L'T' : L'F';
        b += (f.titleEmpty > 0) ? L'T' : L'F';

        b += (f.wDip < kSmallW && f.hDip < kSmallH) ? L'T' : L'F';
        b += (f.wDip > kLargeW || f.hDip > kLargeH) ? L'T' : L'F';
        b += (f.pathTemp > 0) ? L'T' : L'F';
        b += (f.pathRoaming > 0) ? L'T' : L'F';

        b += (f.clsHexRatio > kHexRatioThreshold) ? L'T' : L'F';
        b += (f.procAgeSec >= 0 && f.procAgeSec < kYoungProcessSec) ? L'T' : L'F';
        b += (!f.path.empty() && !IsFileSignedCached(f.path)) ? L'T' : L'F';
        b += (f.userIdle > 0) ? L'T' : L'F';
        b += (f.farFromMouse > 0) ? L'T' : L'F';
        b += (f.parentExplorer > 0) ? L'T' : L'F';
        b += (f.parentSystem > 0) ? L'T' : L'F';
        b += (f.parentUnknown > 0) ? L'T' : L'F';
        b += (f.sameProcAsPrevForeground > 0) ? L'T' : L'F';
        return b;
    }

    // 加权打分：输入 Features，返回 ≥0 的分数并填充 detail 明细；命中硬过滤（基础设施类/零尺寸）时返回 0 且 detail 为 *_skip。
    inline int ScoreWindow(Features const& f, std::wstring& detail)
    {
        if (f.cls == L"consolewindowclass" ||
            f.cls.find(L"chrome_widgetwin") != std::wstring::npos ||
            f.cls.find(L"microsoftwindowstooltip") != std::wstring::npos ||
            f.cls.find(L"pseudoconsole") != std::wstring::npos ||
            f.cls.rfind(L"hwndwrapper", 0) == 0 ||
            f.cls == L"tooltip" || f.cls.rfind(L"tooltip_", 0) == 0 ||
            f.cls == L"msctfime ui" || f.cls == L"default ime" ||
            f.cls == L"dragvisualwindow" ||
            f.cls == L"#32768" ||
            f.cls == L"shell_systemdialog" ||
            f.cls == L"shell_systemdialogproxy" ||
            f.cls == L"shell_systemdim")
        {
            detail = L"infra_class_skip";
            return 0;
        }

        float wDip = f.wDip;   // 逻辑像素，阈值按 DIP
        float hDip = f.hDip;
        if (wDip <= 0 || hDip <= 0)
        {
            detail = L"zero_size_skip";
            return 0;
        }

        float s = 0;
        auto add = [&](float w, const wchar_t* name) {
            if (w == 0.f) return;
            s += w;
            detail += name;
            detail += (w > 0.f) ? (L"+" + std::to_wstring(int(w))) : std::to_wstring(int(w));
            detail += L" ";
            };

        if (f.hasOwner > 0) add(g_weights.owner, L"owner");
        if (f.toolWin > 0) add(g_weights.toolWin, L"toolwin");
        if (f.topmost > 0) add(g_weights.topmost, L"topmost");
        if (f.noActivate > 0) add(g_weights.noActivate, L"noactivate");

        if (f.resizable > 0) add(g_weights.resizable, L"resizable");
        else add(g_weights.notResizable, L"notresizable");

        if (f.hasMinMax > 0) add(g_weights.hasMinMax, L"minmax");
        else add(g_weights.noMinMax, L"nominmax");

        if (f.captionSysmenu > 0) add(g_weights.captionSysmenu, L"capsys");

        if (wDip < kSmallW && hDip < kSmallH) add(g_weights.smallWindow, L"small");
        if (wDip > kLargeW || hDip > kLargeH) add(g_weights.largeWindow, L"large");

        if (f.titleEmpty > 0) add(g_weights.titleEmpty, L"notitle");
        if (f.titleKwHits > 0)
            add(g_weights.titleKwHit * (f.titleKwHits > 2 ? 2 : f.titleKwHits), L"kw");

        if (f.clsHexRatio > kHexRatioThreshold) add(g_weights.clsHex, L"hexclass");
        if (f.pathTemp > 0) add(g_weights.pathTemp, L"temp");
        if (f.pathRoaming > 0) add(g_weights.pathRoaming, L"roaming");
        if (f.procAgeSec >= 0 && f.procAgeSec < kYoungProcessSec) add(g_weights.youngProcess, L"young");

        if (f.userIdle > 0) add(g_weights.userIdle, L"idle");
        if (f.farFromMouse > 0) add(g_weights.farFromMouse, L"far_mouse");
        if (f.farFromMouse == 0.f) add(g_weights.mouseClose, L"mouse_close");

        if (f.parentExplorer > 0) add(g_weights.parentExplorer, L"parent_explorer");
        if (f.parentSystem > 0) add(g_weights.parentSystem, L"parent_system");
        if (f.parentUnknown > 0) add(g_weights.parentUnknown, L"parent_unknown");
        if (f.sameProcAsPrevForeground > 0) add(g_weights.sameProcAsPrevForeground, L"same_fg");

        if (!f.path.empty()) {
            bool signed_ = IsFileSignedCached(f.path);
            if (signed_) {
                add(g_weights.signedExe, L"signed");
            }
            else {
                add(g_weights.unsignedExe, L"unsigned");
                if (f.pathTemp > 0 || f.pathRoaming > 0) {
                    add(g_weights.unsignedUserDir, L"unsigned_userdir");
                }
            }
        }
        else {
            detail += L"no_path ";
        }

        return int(s > 0 ? s : 0);
    }
}