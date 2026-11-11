#pragma once

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <uiautomationclient.h>
#include <string>

#pragma comment(lib, "uiautomationcore.lib")

// 读取被拦截弹窗的正文，供日志展示。
//
// 约束：
//   * 只读、best-effort，任何一步失败都返回空串，绝不抛异常；
//   * 所有跨进程调用都带超时（UIA 设 ConnectionTimeout，子窗口用 SendMessageTimeout），
//     整次读取有硬预算，宁可少读几个控件也不拖住调用线程；
//   * 必须在弹窗销毁前调用（隐藏不影响读取），由调用方保证时序。
namespace WindowText
{
    inline constexpr size_t kMaxChars = 200;          // 最多保留字符数
    inline constexpr DWORD  kChildTimeoutMs = 40;     // 单个子窗口 WM_GETTEXT 上限
    inline constexpr DWORD  kTotalBudgetMs = 120;     // 整次读取预算
    inline constexpr int    kMaxElements = 12;        // UIA 最多遍历的元素数
    inline constexpr int    kMaxChildren = 8;         // 子窗口枚举上限

    // 换行/制表符压成空格、连续空格折叠、截断到 kMaxChars
    inline std::wstring Sanitize(std::wstring s)
    {
        std::wstring out;
        out.reserve(s.size());
        bool prevSpace = true;
        for (wchar_t c : s)
        {
            if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            if (c == L' ')
            {
                if (prevSpace) continue;
                prevSpace = true;
            }
            else prevSpace = false;
            out += c;
        }
        while (!out.empty() && out.back() == L' ') out.pop_back();
        if (out.size() > kMaxChars)
        {
            out.resize(kMaxChars);
            out += L"…";
        }
        return out;
    }

    inline void AppendPart(std::wstring& out, std::wstring const& part)
    {
        if (part.empty()) return;
        if (out.find(part) != std::wstring::npos) return;   // 顶层 Name 常与标题重复
        if (!out.empty()) out += L" / ";
        out += part;
    }

    // UIA 客户端：只在首次调用时创建；连接/事务超时压到 200ms，避免目标无响应时长时间挂起。
    inline IUIAutomation* GetUia()
    {
        static IUIAutomation* s_uia = nullptr;
        static bool s_tried = false;
        if (s_tried) return s_uia;
        s_tried = true;

        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return nullptr;

        IUIAutomation* uia = nullptr;
        if (FAILED(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&uia))) || !uia)
        {
            return nullptr;
        }

        IUIAutomation2* uia2 = nullptr;
        if (SUCCEEDED(uia->QueryInterface(IID_PPV_ARGS(&uia2))) && uia2)
        {
            uia2->put_ConnectionTimeout(200);
            uia2->put_TransactionTimeout(200);
            uia2->Release();
        }

        s_uia = uia;
        return s_uia;
    }

    // 单个元素的文本：先探测再取，Name -> ValuePattern -> TextPattern
    inline std::wstring ReadElement(IUIAutomationElement* el)
    {
        if (!el) return {};

        std::wstring text;
        VARIANT v{};
        ::VariantInit(&v);

        if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_NamePropertyId, &v)) &&
            v.vt == VT_BSTR && v.bstrVal)
        {
            text = v.bstrVal;
        }
        ::VariantClear(&v);

        if (text.empty())
        {
            ::VariantInit(&v);
            BOOL hasValue = FALSE;
            if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_IsValuePatternAvailablePropertyId, &v)) &&
                v.vt == VT_BOOL)
            {
                hasValue = v.boolVal;
            }
            ::VariantClear(&v);

            if (hasValue)
            {
                IUIAutomationValuePattern* vp = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp))) && vp)
                {
                    BSTR val = nullptr;
                    if (SUCCEEDED(vp->get_CurrentValue(&val)) && val)
                    {
                        text = val;
                        ::SysFreeString(val);
                    }
                    vp->Release();
                }
            }
        }

        if (text.empty())
        {
            ::VariantInit(&v);
            BOOL hasText = FALSE;
            if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId, &v)) &&
                v.vt == VT_BOOL)
            {
                hasText = v.boolVal;
            }
            ::VariantClear(&v);

            if (hasText)
            {
                IUIAutomationTextPattern* tp = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp)
                {
                    IUIAutomationTextRange* range = nullptr;
                    if (SUCCEEDED(tp->get_DocumentRange(&range)) && range)
                    {
                        BSTR t = nullptr;
                        if (SUCCEEDED(range->GetText(2048, &t)) && t)
                        {
                            text = t;
                            ::SysFreeString(t);
                        }
                        range->Release();
                    }
                    tp->Release();
                }
            }
        }

        return text;
    }

    inline std::wstring ReadViaUia(HWND hwnd, ULONGLONG startTick)
    {
        IUIAutomation* uia = GetUia();
        if (!uia) return {};

        IUIAutomationElement* root = nullptr;
        if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) return {};

        std::wstring out;
        AppendPart(out, ReadElement(root));

        IUIAutomationCondition* cond = nullptr;
        IUIAutomationElementArray* arr = nullptr;
        if (SUCCEEDED(uia->CreateTrueCondition(&cond)) && cond &&
            SUCCEEDED(root->FindAll(TreeScope_Subtree, cond, &arr)) && arr)
        {
            int len = 0;
            arr->get_Length(&len);
            for (int i = 0; i < len && i < kMaxElements; ++i)
            {
                if (::GetTickCount64() - startTick > kTotalBudgetMs) break;
                if (out.size() >= kMaxChars) break;

                IUIAutomationElement* child = nullptr;
                if (SUCCEEDED(arr->GetElement(i, &child)) && child)
                {
                    AppendPart(out, ReadElement(child));
                    child->Release();
                }
            }
            arr->Release();
        }
        if (cond) cond->Release();
        root->Release();

        return out;
    }

    struct EnumCtx
    {
        std::wstring* out;
        ULONGLONG startTick;
        int count;
    };

    inline BOOL CALLBACK EnumChildProc(HWND child, LPARAM lp)
    {
        auto* ctx = reinterpret_cast<EnumCtx*>(lp);
        if (::GetTickCount64() - ctx->startTick > kTotalBudgetMs) return FALSE;
        if (++ctx->count > kMaxChildren) return FALSE;

        wchar_t buf[512]{};
        DWORD_PTR unused = 0;
        LRESULT len = ::SendMessageTimeoutW(child, WM_GETTEXT,
            static_cast<WPARAM>(sizeof(buf) / sizeof(buf[0]) - 1), reinterpret_cast<LPARAM>(buf),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, kChildTimeoutMs, &unused);
        if (len && buf[0]) AppendPart(*ctx->out, buf);
        return TRUE;
    }

    // 回退路径：UIA 拿不到时直接向子控件要 WM_GETTEXT（受控超时，目标是死窗口时立即放弃）
    inline std::wstring ReadViaMessages(HWND hwnd, ULONGLONG startTick)
    {
        std::wstring out;
        EnumCtx ctx{ &out, startTick, 0 };
        ::EnumChildWindows(hwnd, EnumChildProc, reinterpret_cast<LPARAM>(&ctx));
        return out;
    }

    inline std::wstring Read(HWND hwnd)
    {
        if (!hwnd || !::IsWindow(hwnd)) return {};

        ULONGLONG start = ::GetTickCount64();
        std::wstring text = ReadViaUia(hwnd, start);
        if (text.empty() && ::GetTickCount64() - start < kTotalBudgetMs)
        {
            text = ReadViaMessages(hwnd, start);
        }
        return Sanitize(std::move(text));
    }
}
