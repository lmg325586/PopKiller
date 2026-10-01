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
#include <wintrust.h>
#include <tlhelp32.h>

#pragma comment(lib, "wintrust.lib")

namespace HeuristicScorer
{
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

        inline float CalcUserIdle(DWORD evTime)
        {
            LASTINPUTINFO lii{}; lii.cbSize = sizeof(lii);
            if (!::GetLastInputInfo(&lii)) return 0.f;

            LONG diff = static_cast<LONG>(evTime) - static_cast<LONG>(lii.dwTime);
            long long idleMs = diff;
            if (idleMs < 0) idleMs = 0;

            return (idleMs > 5000) ? 1.f : 0.f;
        }

        // scale = DPI/96：阈值按 300 逻辑像素换算成物理像素（300*scale）
        inline float CalcFarFromMouse(RECT const& rc, float scale)
        {
            POINT cpt{}; ::GetCursorPos(&cpt);
            int dx = (cpt.x < rc.left) ? (rc.left - cpt.x) : (cpt.x > rc.right ? cpt.x - rc.right : 0);
            int dy = (cpt.y < rc.top) ? (rc.top - cpt.y) : (cpt.y > rc.bottom ? cpt.y - rc.bottom : 0);
            long long d2 = (long long)dx * dx + (long long)dy * dy;
            long long lim = (long long)(300.f * scale);
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
        inline ProcTable& Proc()
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
        inline ParentCache& ParentC()
        {
            static ParentCache* c = new ParentCache();
            return *c;
        }

        // 返回窗口所在进程的“父进程 exe 名”（小写）；失败返回空
        inline std::wstring GetParentExe(DWORD pid)
        {
            if (!pid) return {};
            unsigned long long ct = ProcCreateTime(pid);
            ParentCache& pc = ParentC();
            {
                std::lock_guard<std::mutex> l(pc.mtx);
                auto it = pc.map.find(pid);
                if (it != pc.map.end() && it->second.first == ct) return it->second.second;
            }
            DWORD ppid = 0;
            ProcTable& pt = Proc();
            {
                std::lock_guard<std::mutex> l(pt.mtx);
                if (::GetTickCount64() - pt.builtMs > 2000) RebuildProcTable(pt);
                auto it = pt.ppid.find(pid);
                if (it != pt.ppid.end()) ppid = it->second;
            }
            std::wstring parent = ProcessNameByPid(ppid);
            {
                std::lock_guard<std::mutex> l(pc.mtx);
                if (pc.map.size() > 4096) pc.map.clear();
                pc.map[pid] = { ct, parent };
            }
            return parent;
        }
    }

    // ===== 权重表 =====
    struct Weights
    {
        float owner = 15;
        float toolWin = 12;
        float topmost = 20;
        float noActivate = 5;
        float notResizable = 12;
        float resizable = -15;
        float noMinMax = 12;
        float hasMinMax = -5;
        float captionSysmenu = -10;
        float smallWindow = 28;
        float largeWindow = -20;
        float titleEmpty = 20;
        float titleKwHit = 46;
        float clsHex = 10;
        float pathTemp = 20;
        float pathRoaming = 12;
        float youngProcess = 5;
        float unsignedExe = 12;
        float unsignedUserDir = 25;
        float signedExe = -5;
        float userIdle = 15;
        float farFromMouse = 10;
        float mouseClose = -25;
        float parentExplorer = -8;              // 父进程是 explorer（外壳/用户启动）→ 更像正常窗
        float parentSystem = 8;                 // 父进程是系统/后台宿主 → 可疑
        float parentUnknown = 4;                // 取不到父进程 → 轻微可疑
        float sameProcAsPrevForeground = -12;   // 与“刚在前台的进程”同进程 → 很可能是自家弹窗
    };
    inline Weights g_w{};

    struct Features
    {
        float hasOwner, toolWin, topmost, noActivate;
        float resizable, hasMinMax, captionSysmenu;
        float wDip, hDip;      // 逻辑像素宽高（已按窗口 DPI 归一）
        float dpiScale;        // 窗口 DPI / 96
        float titleLen, titleEmpty, titleDigitRatio, titleKwHits;
        float clsLen, clsHexRatio;
        float pathTemp, pathRoaming, pathDepth, exeDigitRatio;
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
        if (cls.size() < 6) return false;
        return HexRatio(cls) >= 0.8f || DigitRatio(cls) >= 0.6f;
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

    // 兼容旧名（DOCUMENTATION.md 中的公开接口），语义不变
    inline bool IsFileSigned(std::wstring const& path) { return VerifyFileSignature(path); }

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
        f.titleLen = float(title.size());
        f.titleEmpty = title.empty() ? 1.f : 0.f;
        f.titleDigitRatio = DigitRatio(title);
        float kw = 0.f;
        for (auto k : { L"热点", L"资讯", L"推荐", L"广告", L"优惠", L"领取", L"pop", L"ads" })
            if (title.find(k) != std::wstring::npos) kw += 1.f;
        f.titleKwHits = kw;

        std::wstring cls = detail::GetClass(hwnd);
        f.clsLen = float(cls.size());
        f.clsHexRatio = HexRatio(cls);
        f.cls = cls;

        f.path = detail::GetProcessPath(hwnd);
        f.pathTemp = f.path.find(L"\\appdata\\local\\temp\\") != std::wstring::npos ? 1.f : 0.f;
        f.pathRoaming = f.path.find(L"\\appdata\\roaming\\") != std::wstring::npos ? 1.f : 0.f;
        f.pathDepth = float(std::count(f.path.begin(), f.path.end(), L'\\'));
        auto pos = f.path.find_last_of(L"\\/");
        std::wstring exe = (pos == std::wstring::npos) ? f.path : f.path.substr(pos + 1);
        f.exeDigitRatio = DigitRatio(exe);
        f.procAgeSec = ProcessAgeSeconds(hwnd);

        DWORD pid = 0; ::GetWindowThreadProcessId(hwnd, &pid);
        std::wstring parent = detail::GetParentExe(pid);
        f.parentExplorer = (parent == L"explorer.exe") ? 1.f : 0.f;
        f.parentSystem = (parent == L"services.exe" || parent == L"svchost.exe" || parent == L"wininit.exe" ||
            parent == L"winlogon.exe" || parent == L"lsass.exe" || parent == L"taskhostw.exe" ||
            parent == L"runtimebroker.exe") ? 1.f : 0.f;
        f.parentUnknown = parent.empty() ? 1.f : 0.f;
        f.sameProcAsPrevForeground = (prevForegroundPid != 0 && pid == prevForegroundPid) ? 1.f : 0.f;

        f.userIdle = detail::CalcUserIdle(evTime);
        f.farFromMouse = detail::CalcFarFromMouse(rc, f.dpiScale);
        return f;
    }

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

        b += (f.wDip < 400 && f.hDip < 300) ? L'T' : L'F';
        b += (f.wDip > 800 || f.hDip > 600) ? L'T' : L'F';
        b += (f.pathTemp > 0) ? L'T' : L'F';
        b += (f.pathRoaming > 0) ? L'T' : L'F';

        b += (f.clsHexRatio > 0.8f) ? L'T' : L'F';
        b += (f.procAgeSec >= 0 && f.procAgeSec < 120) ? L'T' : L'F';
        b += (!f.path.empty() && !IsFileSignedCached(f.path)) ? L'T' : L'F';
        b += (f.userIdle > 0) ? L'T' : L'F';
        b += (f.farFromMouse > 0) ? L'T' : L'F';
        b += (f.parentExplorer > 0) ? L'T' : L'F';
        b += (f.parentSystem > 0) ? L'T' : L'F';
        b += (f.parentUnknown > 0) ? L'T' : L'F';
        b += (f.sameProcAsPrevForeground > 0) ? L'T' : L'F';
        return b;
    }

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

        float wpx = f.wDip;   // 逻辑像素，阈值按 DIP
        float hpx = f.hDip;
        if (wpx <= 0 || hpx <= 0)
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

        if (f.hasOwner > 0) add(g_w.owner, L"owner");
        if (f.toolWin > 0) add(g_w.toolWin, L"toolwin");
        if (f.topmost > 0) add(g_w.topmost, L"topmost");
        if (f.noActivate > 0) add(g_w.noActivate, L"noactivate");

        if (f.resizable > 0) add(g_w.resizable, L"resizable");
        else add(g_w.notResizable, L"notresizable");

        if (f.hasMinMax > 0) add(g_w.hasMinMax, L"minmax");
        else add(g_w.noMinMax, L"nominmax");

        if (f.captionSysmenu > 0) add(g_w.captionSysmenu, L"capsys");

        if (wpx < 400 && hpx < 300) add(g_w.smallWindow, L"small");
        if (wpx > 800 || hpx > 600) add(g_w.largeWindow, L"large");

        if (f.titleEmpty > 0) add(g_w.titleEmpty, L"notitle");
        if (f.titleKwHits > 0)
            add(g_w.titleKwHit * (f.titleKwHits > 2 ? 2 : f.titleKwHits), L"kw");

        if (f.clsHexRatio > 0.8f) add(g_w.clsHex, L"hexclass");
        if (f.pathTemp > 0) add(g_w.pathTemp, L"temp");
        if (f.pathRoaming > 0) add(g_w.pathRoaming, L"roaming");
        if (f.procAgeSec >= 0 && f.procAgeSec < 120) add(g_w.youngProcess, L"young");

        if (f.userIdle > 0) add(g_w.userIdle, L"idle");
        if (f.farFromMouse > 0) add(g_w.farFromMouse, L"far_mouse");
        if (f.farFromMouse == 0.f) add(g_w.mouseClose, L"mouse_close");

        if (f.parentExplorer > 0) add(g_w.parentExplorer, L"parent_explorer");
        if (f.parentSystem > 0) add(g_w.parentSystem, L"parent_system");
        if (f.parentUnknown > 0) add(g_w.parentUnknown, L"parent_unknown");
        if (f.sameProcAsPrevForeground > 0) add(g_w.sameProcAsPrevForeground, L"same_fg");

        if (!f.path.empty()) {
            bool signed_ = IsFileSignedCached(f.path);
            if (signed_) {
                add(g_w.signedExe, L"signed");
            }
            else {
                add(g_w.unsignedExe, L"unsigned");
                if (f.pathTemp > 0 || f.pathRoaming > 0) {
                    add(g_w.unsignedUserDir, L"unsigned_userdir");
                }
            }
        }
        else {
            detail += L"no_path ";
        }

        return int(s > 0 ? s : 0);
    }
}