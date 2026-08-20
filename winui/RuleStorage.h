#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include "RuleTypes.h"

namespace PopupBlocker
{
    inline std::wstring RulesPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        return p.substr(0, pos + 1) + L"rules.json";
    }

    inline bool ReadUtf8File(std::wstring const& p, std::wstring& out)
    {
        HANDLE hf = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER sz{};
        ::GetFileSizeEx(hf, &sz);
        if (sz.QuadPart == 0) {
            ::CloseHandle(hf);
            out.clear();
            return true;
        }

        std::vector<char> buffer(static_cast<size_t>(sz.QuadPart));
        DWORD rd{};
        if (!::ReadFile(hf, buffer.data(), static_cast<DWORD>(buffer.size()), &rd, nullptr) || rd == 0) {
            ::CloseHandle(hf);
            return false;
        }
        ::CloseHandle(hf);

        char* data = buffer.data();
        int len = static_cast<int>(rd);
        if (len >= 3 &&
            (unsigned char)data[0] == 0xEF &&
            (unsigned char)data[1] == 0xBB &&
            (unsigned char)data[2] == 0xBF) {
            data += 3;
            len -= 3;
        }

        int need = ::MultiByteToWideChar(CP_UTF8, 0, data, len, nullptr, 0);
        if (need <= 0) return false;

        out.assign(static_cast<size_t>(need), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, data, len, out.data(), need);
        return true;
    }

    inline bool WriteUtf8File(std::wstring const& p, std::wstring const& text)
    {
        if (text.empty()) {
            HANDLE hf = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (hf != INVALID_HANDLE_VALUE) ::CloseHandle(hf);
            return true;
        }
        int need = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return false;

        std::vector<char> buffer(need);
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), buffer.data(), need, nullptr, nullptr);

        HANDLE hf = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;

        DWORD wr{};
        bool ok = ::WriteFile(hf, buffer.data(), static_cast<DWORD>(buffer.size()), &wr, nullptr);
        ::CloseHandle(hf);
        return ok;
    }

    inline bool ParseRuleLine(std::wstring const& line, Rule& r)
    {
        size_t p1 = line.find(L':');
        if (p1 == std::wstring::npos) return false;
        std::wstring first = line.substr(0, p1);
        size_t p2 = line.find(L':', p1 + 1);

        if (first == L"B" || first == L"W") {
            r.isWhitelist = (first == L"W");
            if (p2 == std::wstring::npos) return false;
            std::wstring f_str = line.substr(p1 + 1, p2 - p1 - 1);
            size_t p3 = line.find(L':', p2 + 1);
            std::wstring m_str, p_str;
            if (p3 == std::wstring::npos) { m_str = L"contains"; p_str = line.substr(p2 + 1); }
            else { m_str = line.substr(p2 + 1, p3 - p2 - 1); p_str = line.substr(p3 + 1); }

            if (f_str == L"exe") r.field = RuleField::Exe;
            else if (f_str == L"path") r.field = RuleField::Path;
            else if (f_str == L"title") r.field = RuleField::Title;
            else if (f_str == L"class") r.field = RuleField::Class;
            else return false;

            if (m_str == L"exact") r.mode = MatchMode::Exact;
            else if (m_str == L"wildcard") r.mode = MatchMode::Wildcard;
            else r.mode = MatchMode::Contains;
            r.pattern = Lower(p_str);
        }
        else {
            r.isWhitelist = false;
            if (first == L"exe") r.field = RuleField::Exe;
            else if (first == L"title") r.field = RuleField::Title;
            else if (first == L"class") r.field = RuleField::Class;
            else return false;
            r.mode = MatchMode::Contains;
            r.pattern = Lower(line.substr(p1 + 1));
        }
        return !r.pattern.empty();
    }

    inline bool LoadRulesJson(std::vector<Rule>& out)
    {
        std::wstring text;
        if (!ReadUtf8File(RulesPath(), text) || text.empty()) return false;
        try {
            using namespace winrt::Windows::Data::Json;
            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(winrt::hstring(text), root) || !root || !root.HasKey(L"rules")) return false;
            JsonArray arr = root.GetNamedArray(L"rules");
            for (uint32_t i = 0; i < arr.Size(); ++i) {
                JsonObject o = arr.GetObjectAt(i);
                Rule r;
                r.isWhitelist = o.GetNamedString(L"list", L"B") == L"W";
                std::wstring f(o.GetNamedString(L"field", L"exe"));
                if (f == L"exe") r.field = RuleField::Exe;
                else if (f == L"path") r.field = RuleField::Path;
                else if (f == L"title") r.field = RuleField::Title;
                else if (f == L"class") r.field = RuleField::Class;
                std::wstring m(o.GetNamedString(L"mode", L"contains"));
                if (m == L"exact") r.mode = MatchMode::Exact;
                else if (m == L"wildcard") r.mode = MatchMode::Wildcard;
                r.pattern = Lower(std::wstring(o.GetNamedString(L"pattern", L"")));
                if (!r.pattern.empty()) out.push_back(r);
            }
            return true;
        }
        catch (...) { return false; }
    }

    inline bool SaveRulesJson(std::vector<Rule> const& rules)
    {
        using namespace winrt::Windows::Data::Json;
        JsonArray arr;
        for (auto const& r : rules) {
            JsonObject o;
            o.SetNamedValue(L"list", JsonValue::CreateStringValue(r.isWhitelist ? L"W" : L"B"));
            const wchar_t* f = L"exe";
            switch (r.field) {
            case RuleField::Path:  f = L"path";  break;
            case RuleField::Title: f = L"title"; break;
            case RuleField::Class: f = L"class"; break;
            default: break;
            }
            const wchar_t* m = L"contains";
            switch (r.mode) {
            case MatchMode::Exact:    m = L"exact";    break;
            case MatchMode::Wildcard: m = L"wildcard"; break;
            default: break;
            }
            o.SetNamedValue(L"field", JsonValue::CreateStringValue(f));
            o.SetNamedValue(L"mode", JsonValue::CreateStringValue(m));
            o.SetNamedValue(L"pattern", JsonValue::CreateStringValue(r.pattern));
            arr.Append(o);
        }
        JsonObject root;
        root.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
        root.SetNamedValue(L"rules", arr);
        return WriteUtf8File(RulesPath(), std::wstring(root.Stringify()));
    }

    inline void EnsureDefaultRules()
    {
        if (::GetFileAttributesW(RulesPath().c_str()) != INVALID_FILE_ATTRIBUTES) return;

        std::vector<Rule> rules;
        for (auto d : { L"B:exe:contains:flashcenter.exe", L"B:exe:contains:minipage.exe",
                        L"B:title:contains:\u70ED\u70B9", L"W:exe:contains:explorer.exe" }) {
            Rule r;
            if (ParseRuleLine(d, r)) rules.push_back(r);
        }
        SaveRulesJson(rules);
    }
}