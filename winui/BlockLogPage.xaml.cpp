#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include <sstream>
#include <vector>
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

        if (data.size() >= 3 &&
            (unsigned char)data[0] == 0xEF &&
            (unsigned char)data[1] == 0xBB &&
            (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);

        int need = ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need) - 1, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, wide.data(), need);
        return wide;
    }
}

namespace winrt::winui::implementation
{
    BlockLogPage::BlockLogPage()
    {
        InitializeComponent();
        Load();
    }

    void BlockLogPage::Load()
    {
        LogList().Items().Clear();

        std::wstringstream ss(ReadLogText());
        std::wstring line;
        std::vector<std::wstring> lines;
        while (std::getline(ss, line, L'\n'))
        {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            LogList().Items().Append(box_value(hstring(*it)));
    }

    void BlockLogPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Load();
    }

    void BlockLogPage::Clear_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"wb") == 0 && f)
            ::fclose(f);
        Load();
    }
}