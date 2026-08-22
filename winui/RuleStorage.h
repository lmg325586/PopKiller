#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "RuleTypes.h"
#include "vendor/json.hpp"

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

    inline std::string WStringToUtf8(std::wstring const& wstr) {
        if (wstr.empty()) return {};
        int need = ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return {};
        std::string str(need, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), str.data(), need, nullptr, nullptr);
        return str;
    }

    inline std::wstring Utf8ToWString(std::string const& str) {
        if (str.empty()) return {};
        int need = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
        if (need <= 0) return {};
        std::wstring wstr(need, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), wstr.data(), need);
        return wstr;
    }

    inline bool ReadFileToUtf8String(std::wstring const& p, std::string& out) {
        HANDLE hf = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{}; ::GetFileSizeEx(hf, &sz);
        if (sz.QuadPart == 0) { ::CloseHandle(hf); out.clear(); return true; }
        std::vector<char> buffer(static_cast<size_t>(sz.QuadPart));
        DWORD rd{};
        if (!::ReadFile(hf, buffer.data(), static_cast<DWORD>(buffer.size()), &rd, nullptr) || rd == 0) {
            ::CloseHandle(hf); return false;
        }
        ::CloseHandle(hf);
        char* data = buffer.data();
        int len = static_cast<int>(rd);
        if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            data += 3; len -= 3;
        }
        out.assign(data, len);
        return true;
    }

    inline bool WriteUtf8StringToFile(std::wstring const& p, std::string const& text) {
        HANDLE hf = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        DWORD wr{};
        bool ok = ::WriteFile(hf, text.data(), static_cast<DWORD>(text.size()), &wr, nullptr);
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

    inline bool ParseRulesFromJsonString(std::string const& utf8_text, std::vector<Rule>& out)
    {
        if (utf8_text.empty()) return false;
        try {
            auto j = nlohmann::json::parse(utf8_text);
            if (!j.contains("rules") || !j["rules"].is_array()) return false;

            for (auto& item : j["rules"]) {
                Rule r;
                r.isWhitelist = item.value("list", "B") == "W";

                std::string f = item.value("field", "exe");
                if (f == "exe") r.field = RuleField::Exe;
                else if (f == "path") r.field = RuleField::Path;
                else if (f == "title") r.field = RuleField::Title;
                else if (f == "class") r.field = RuleField::Class;

                std::string m = item.value("mode", "contains");
                if (m == "exact") r.mode = MatchMode::Exact;
                else if (m == "wildcard") r.mode = MatchMode::Wildcard;

                r.fromCommunity = item.value("source", "") == "community";
                r.pattern = Lower(Utf8ToWString(item.value("pattern", "")));
                if (!r.pattern.empty()) out.push_back(r);
            }
            return true;
        }
        catch (...) { return false; }
    }

    inline bool LoadRulesJson(std::vector<Rule>& out, std::vector<std::wstring>& removedOut)
    {
        std::string utf8_text;
        if (!ReadFileToUtf8String(RulesPath(), utf8_text)) return false;
        if (utf8_text.empty()) return false;
        try {
            auto j = nlohmann::json::parse(utf8_text);
            if (!ParseRulesFromJsonString(utf8_text, out)) return false;
            if (j.contains("communityRemoved") && j["communityRemoved"].is_array()) {
                for (auto& s : j["communityRemoved"])
                    removedOut.push_back(Utf8ToWString(s.get<std::string>()));
            }
            return true;
        }
        catch (...) { return false; }
    }

    inline std::string SerializeRules(std::vector<Rule> const& rules, std::vector<std::wstring> const& removed)
    {
        nlohmann::json j;
        j["version"] = 1;
        j["rules"] = nlohmann::json::array();

        for (auto const& r : rules) {
            nlohmann::json item;
            item["list"] = r.isWhitelist ? "W" : "B";

            const char* f = "exe";
            switch (r.field) {
            case RuleField::Path:  f = "path";  break;
            case RuleField::Title: f = "title"; break;
            case RuleField::Class: f = "class"; break;
            default: break;
            }
            item["field"] = f;

            const char* m = "contains";
            switch (r.mode) {
            case MatchMode::Exact:    m = "exact";    break;
            case MatchMode::Wildcard: m = "wildcard"; break;
            default: break;
            }
            item["mode"] = m;
            item["pattern"] = WStringToUtf8(r.pattern);
            if (r.fromCommunity) item["source"] = "community";

            j["rules"].push_back(item);
        }

        j["communityRemoved"] = nlohmann::json::array();
        for (auto const& k : removed)
            j["communityRemoved"].push_back(WStringToUtf8(k));

        return j.dump(4);
    }

    inline bool SaveRulesJson(std::vector<Rule> const& rules, std::vector<std::wstring> const& removed)
    {
        return WriteUtf8StringToFile(RulesPath(), SerializeRules(rules, removed));
    }

    inline void EnsureDefaultRules()
    {
        if (::GetFileAttributesW(RulesPath().c_str()) != INVALID_FILE_ATTRIBUTES) return;

        std::vector<Rule> rules;
        for (auto d : { L"B:exe:contains:flashcenter.exe", L"B:exe:contains:minipage.exe",
                        L"B:title:contains:\u70ED\u70B9",
                        L"W:exe:contains:explorer.exe" }) {
            Rule r;
            if (ParseRuleLine(d, r)) rules.push_back(r);
        }
        SaveRulesJson(rules, {});
    }
}