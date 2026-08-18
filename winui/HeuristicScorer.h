#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <wintrust.h>

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
    }

    // ===== 权重表 =====
    struct Weights
    {
        float owner = 15;
        float toolWin = 12;
        float topmost = 20;
        float noActivate = 5;
        float notResizable = 10;
        float resizable = -15;
        float noMinMax = 10;
        float hasMinMax = -10;
        float captionSysmenu = -10;
        float smallWindow = 19;
        float largeWindow = -20;
        float titleEmpty = 10;
        float titleKwHit = 46;
        float clsHex = 10;
        float pathTemp = 20;
        float pathRoaming = 12;
        float youngProcess = 5;
        float unsignedExe = 12;
        float unsignedUserDir = 25;
        float signedExe = -10;
    };
    inline Weights g_w{};

    struct Features
    {
        float hasOwner, toolWin, topmost, noActivate;
        float resizable, hasMinMax, captionSysmenu;
        float wNorm, hNorm;
        float titleLen, titleEmpty, titleDigitRatio, titleKwHits;
        float clsLen, clsHexRatio;
        float pathTemp, pathRoaming, pathDepth, exeDigitRatio;
        float procAgeSec;
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

    inline bool IsFileSigned(std::wstring const& path)
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

        return res != TRUST_E_NOSIGNATURE;
    }

    inline std::mutex SigMx;
    inline std::unordered_map<std::wstring, bool> SigCache;

    inline bool IsFileSignedCached(std::wstring const& path)
    {
        {
            std::lock_guard l(SigMx);
            auto it = SigCache.find(path);
            if (it != SigCache.end()) return it->second;
        }
        bool s = IsFileSigned(path);
        std::lock_guard l(SigMx);
        SigCache[path] = s;
        return s;
    }

    inline Features ExtractFeatures(HWND hwnd)
    {
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
        f.wNorm = float(rc.right - rc.left) / float(::GetSystemMetrics(SM_CXSCREEN));
        f.hNorm = float(rc.bottom - rc.top) / float(::GetSystemMetrics(SM_CYSCREEN));

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
        return f;
    }

    inline int ScoreWindow(Features const& f, std::wstring& detail)
    {
        if (f.cls == L"consolewindowclass" ||
            f.cls.find(L"pseudoconsole") != std::wstring::npos ||
            f.cls.rfind(L"hwndwrapper", 0) == 0 ||
            f.cls == L"tooltip" || f.cls.rfind(L"tooltip_", 0) == 0 ||
            f.cls == L"msctfime ui" || f.cls == L"default ime")
        {
            detail = L"infra_class_skip";
            return 0;
        }

        float wpx = f.wNorm * ::GetSystemMetrics(SM_CXSCREEN);
        float hpx = f.hNorm * ::GetSystemMetrics(SM_CYSCREEN);
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