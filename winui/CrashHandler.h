#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <exception>
#include <string>
#include <cstdlib>
#include <cstdio>
#pragma comment(lib, "dbghelp.lib")

// 全局崩溃处理：捕获未处理异常，在 exe 目录生成 minidump（.dmp）与崩溃日志（crash.log），
// 并弹出提示告知用户文件位置。覆盖 SEH 未处理异常、std::terminate、CRT 无效参数/纯虚调用。
namespace CrashHandler
{
    inline volatile LONG Handling = 0; // 防重入：崩溃处理中再次崩溃直接跳过

    inline std::wstring ExeDir()
    {
        WCHAR path[MAX_PATH]{};
        DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path, n);
        auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p.resize(pos + 1);
        return p;
    }

    inline void WriteUtf8(HANDLE h, std::wstring const& s)
    {
        if (s.empty()) return;
        int len = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return;
        std::string u((size_t)len, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), len, nullptr, nullptr);
        DWORD wrote = 0;
        ::WriteFile(h, u.data(), (DWORD)u.size(), &wrote, nullptr);
    }

    // 生成转储与日志并提示用户；ep 可为空（无异常上下文时仍导出各线程堆栈）
    inline void Report(const wchar_t* reason, EXCEPTION_POINTERS* ep = nullptr)
    {
        if (::InterlockedCompareExchange(&Handling, 1, 0) != 0) return;

        std::wstring dir = ExeDir();

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        wchar_t stamp[32]{};
        ::swprintf_s(stamp, L"%04d%02d%02d_%02d%02d%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        std::wstring dmpPath = dir + L"PopKiller_crash_" + stamp + L".dmp";
        std::wstring logPath = dir + L"crash.log";

        // 1) minidump
        HANDLE hFile = ::CreateFileW(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            MINIDUMP_EXCEPTION_INFORMATION* pmei = nullptr;
            if (ep)
            {
                mei.ThreadId = ::GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers = FALSE;
                pmei = &mei;
            }
            ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile,
                (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithDataSegs),
                pmei, nullptr, nullptr);
            ::CloseHandle(hFile);
        }

        // 2) 崩溃日志（追加）
        HANDLE hLog = ::CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hLog != INVALID_HANDLE_VALUE)
        {
            std::wstring line = L"[" + std::wstring(stamp) + L"] " + (reason ? reason : L"unknown");
            if (ep && ep->ExceptionRecord)
            {
                wchar_t code[64]{};
                ::swprintf_s(code, L" code=0x%08X addr=%p",
                    ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
                line += code;
            }
            line += L" dump=" + dmpPath + L"\r\n";
            WriteUtf8(hLog, line);
            ::CloseHandle(hLog);
        }

        // 3) 用户提示
        std::wstring msg = L"PopKiller 遇到问题需要关闭。\n\n已生成诊断文件：\n";
        msg += dmpPath + L"\n" + logPath;
        msg += L"\n\n请将 .dmp 文件反馈给开发者以帮助定位问题。";
        ::MessageBoxW(nullptr, msg.c_str(), L"PopKiller 异常",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
    }

    inline LONG WINAPI Filter(EXCEPTION_POINTERS* ep)
    {
        Report(L"未处理异常 (SEH)", ep);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    inline void OnTerminate()
    {
        Report(L"C++ 未捕获异常 (std::terminate)");
        ::TerminateProcess(::GetCurrentProcess(), 3);
    }

    inline void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        Report(L"CRT 参数无效 (_invalid_parameter)");
    }

    inline void OnPureCall()
    {
        Report(L"纯虚函数调用 (_purecall)");
    }

    inline void Init()
    {
        ::SetUnhandledExceptionFilter(Filter);
        std::set_terminate(OnTerminate);
        _set_invalid_parameter_handler(OnInvalidParameter);
        _set_purecall_handler(OnPureCall);
    }
}
