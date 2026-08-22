#pragma once
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <string>
#include <microsoft.ui.xaml.window.h>
#include "App.xaml.h"

namespace FilePicker
{
    inline HWND GetAppHwnd()
    {
        auto window = winrt::winui::implementation::App::window;
        if (!window) return nullptr;
        auto native = window.try_as<::IWindowNative>();
        HWND hwnd{};
        if (native && SUCCEEDED(native->get_WindowHandle(&hwnd))) return hwnd;
        return nullptr;
    }

    inline std::wstring PickJsonFile(bool save)
    {
        wchar_t szFile[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetAppHwnd();
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"JSON 文件\0*.json\0所有文件\0*.*\0";
        ofn.lpstrDefExt = L"json";
        ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR)
            : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
        if (save ? ::GetSaveFileNameW(&ofn) : ::GetOpenFileNameW(&ofn))
            return szFile;
        return {};
    }
}