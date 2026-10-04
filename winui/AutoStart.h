#pragma once
#include <windows.h>
#include <string>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Autostart via a shortcut in the user's Startup folder:
//   %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\PopKiller.lnk
// The MSI also declares this shortcut, so it is removed automatically on uninstall.
namespace AutoStart
{
    inline std::wstring StartupShortcutPath()
    {
        PWSTR p = nullptr;
        std::wstring dir;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &p)) && p)
        {
            dir = p;
            ::CoTaskMemFree(p);
        }
        if (dir.empty()) return {};
        if (dir.back() != L'\\') dir += L'\\';
        return dir + L"PopKiller.lnk";
    }

    inline bool IsEnabled()
    {
        std::wstring p = StartupShortcutPath();
        return !p.empty() && ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    inline bool EnableAutoStart()
    {
        static bool comReady = false;
        if (!comReady) { ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); comReady = true; }

        std::wstring lnk = StartupShortcutPath();
        if (lnk.empty()) return false;

        IShellLinkW* link = nullptr;
        if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            return false;

        std::wstring exe = GetSelfPath();
        std::wstring wd = exe;
        auto pos = wd.find_last_of(L"\\/");
        if (pos != std::wstring::npos) wd.resize(pos);

        link->SetPath(exe.c_str());
        link->SetArguments(L"--autostart");
        link->SetWorkingDirectory(wd.c_str());
        link->SetDescription(L"PopKiller");
        link->SetIconLocation(exe.c_str(), 0);

        bool ok = false;
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf))))
        {
            ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            pf->Release();
        }
        link->Release();
        return ok;
    }

    inline bool DisableAutoStart()
    {
        std::wstring p = StartupShortcutPath();
        if (p.empty()) return false;
        if (::DeleteFileW(p.c_str())) return true;
        return ::GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    inline void SyncPath()
    {
        if (IsEnabled()) EnableAutoStart();
    }
}
