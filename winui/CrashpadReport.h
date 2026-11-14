#pragma once

#include <windows.h>

// Crashpad 头内部用 std::min / std::max；windows.h 的 min/max 宏会让解析炸掉。
// 只在本头的作用域里撤掉，不影响其他翻译单元。
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>
#include <map>
#include <vector>
#include <cstdio>

#include "base/files/file_path.h"
#include "client/crashpad_client.h"
#include "client/crash_report_database.h"
#include "util/misc/capture_context.h"

#pragma comment(lib, "client.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

// 用 Crashpad 做崩溃转储：启动早期拉起同目录的 crashpad_handler.exe，
// 原生崩溃由 handler 写入 <exe 目录>\crashpad\；XAML/托管异常走 ReportFatal。
// handler 缺失时整体静默禁用，不影响主程序启动。
namespace CrashpadReport
{
    inline crashpad::CrashpadClient Client;
    inline bool Started = false;
    inline std::wstring DatabaseDir;

    inline std::wstring ExeDir()
    {
        wchar_t path[MAX_PATH]{};
        DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path, n);
        auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p.resize(pos + 1);
        return p;
    }

    inline std::string ToUtf8(std::wstring const& s)
    {
        if (s.empty()) return {};
        int len = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<size_t>(len), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
        return out;
    }

    // 追加一行到 exe 目录的 crash.log（不抛异常，失败静默）
    inline void WriteCrashLog(std::wstring const& line)
    {
        try
        {
            std::wstring path = ExeDir() + L"crash.log";
            HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return;

            SYSTEMTIME st{};
            ::GetLocalTime(&st);
            wchar_t ts[32]{};
            ::swprintf_s(ts, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::string bytes = ToUtf8(std::wstring(ts) + line + L"\r\n");
            DWORD wrote = 0;
            ::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
            ::CloseHandle(h);
        }
        catch (...) {}
    }

    // 上一次会话若留下未处理的 dump，启动时只记录到 crash.log，不弹窗打扰
    inline void LogPendingReports()
    {
        try
        {
            auto db = crashpad::CrashReportDatabase::Initialize(base::FilePath(DatabaseDir));
            if (!db) return;

            std::vector<crashpad::CrashReportDatabase::Report> reports;
            if (db->GetPendingReports(&reports) != crashpad::CrashReportDatabase::kNoError || reports.empty())
                return;

            WriteCrashLog(L"发现 " + std::to_wstring(reports.size()) + L" 份待处理崩溃报告：" + DatabaseDir);
        }
        catch (...) {}
    }

    // 在 App 构造最早期调用。handler 与 exe 同目录，由构建后事件复制。
    inline bool Init()
    {
        try
        {
            std::wstring dir = ExeDir();
            std::wstring handler = dir + L"crashpad_handler.exe";
            if (::GetFileAttributesW(handler.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

            DatabaseDir = dir + L"crashpad";
            ::CreateDirectoryW(DatabaseDir.c_str(), nullptr);

            std::map<std::string, std::string> annotations;
            std::vector<std::string> args;
            args.push_back("--no-rate-limit");
            args.push_back("--no-upload");   // 只落本地，不联网

            Started = Client.StartHandler(
                base::FilePath(handler),
                base::FilePath(DatabaseDir),
                base::FilePath(DatabaseDir),
                std::string(),
                annotations,
                args,
                /*restartable=*/false,
                /*asynchronous_start=*/true);

            if (Started) Client.WaitForHandlerStart(3000);
            LogPendingReports();
            return Started;
        }
        catch (...)
        {
            return false;
        }
    }

    // XAML/托管异常不是原生崩溃，SEH 钩子不会触发：主动让 Crashpad 取一份快照，
    // 写 crash.log、提示用户后结束进程（与旧实现的行为一致）。
    inline void ReportFatal(std::wstring const& reason)
    {
        WriteCrashLog(reason);

        if (Started)
        {
            CONTEXT ctx{};
            crashpad::CaptureContext(&ctx);
            crashpad::CrashpadClient::DumpWithoutCrash(ctx);
        }

        std::wstring msg = L"PopKiller 遇到问题需要关闭。\n\n崩溃报告目录：\n";
        msg += DatabaseDir.empty() ? (ExeDir() + L"crashpad") : DatabaseDir;
        msg += L"\n\n可将目录内的 .dmp 文件反馈给开发者以帮助定位问题。";
        ::MessageBoxW(nullptr, msg.c_str(), L"PopKiller 异常",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
        ::TerminateProcess(::GetCurrentProcess(), 1);
    }
}
