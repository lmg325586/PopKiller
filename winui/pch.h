#pragma once
#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>
#include <string>

// Undefine GetCurrentTime macro to prevent
// conflict with Storyboard::GetCurrentTime
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <wil/cppwinrt_helpers.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.UI.h>

// 取自身 exe 完整路径（循环扩容，绝不因 MAX_PATH 截断）；失败返回空串
inline std::wstring GetSelfPath()
{
    DWORD cap = MAX_PATH;
    for (;;)
    {
        std::wstring buf(static_cast<size_t>(cap), L'\0');
        DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), cap);
        if (n == 0) return {};
        if (n < cap) { buf.resize(n); return buf; }
        if (cap >= 0x10000) return {};
        cap *= 2;
    }
}
