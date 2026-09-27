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
        std::wstring p = GetSelfPath();
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
        std::wstring tmp = p + L".tmp." + std::to_wstring(::GetCurrentThreadId());
        HANDLE hf = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        DWORD wr{};
        bool ok = ::WriteFile(hf, text.data(), static_cast<DWORD>(text.size()), &wr, nullptr);
        ::FlushFileBuffers(hf);
        ::CloseHandle(hf);
        if (!ok || wr != text.size()) { ::DeleteFileW(tmp.c_str()); return false; }
        if (!::MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ::DeleteFileW(tmp.c_str());
            return false;
        }
        return true;
    }

    // 单个条件：field[:mode]:pattern；类名随机写作 class:random（无 pattern）
    inline bool ParseCondition(std::wstring const& seg, RuleCondition& c)
    {
        size_t a = seg.find(L':');
        if (a == std::wstring::npos) return false;
        std::wstring f_str = seg.substr(0, a);
        if (f_str == L"exe") c.field = RuleField::Exe;
        else if (f_str == L"path") c.field = RuleField::Path;
        else if (f_str == L"title") c.field = RuleField::Title;
        else if (f_str == L"class") c.field = RuleField::Class;
        else return false;

        size_t b = seg.find(L':', a + 1);
        if (b == std::wstring::npos) {
            c.mode = MatchMode::Contains;
            c.pattern = Lower(seg.substr(a + 1));
            return !c.pattern.empty();
        }

        std::wstring m_str = seg.substr(a + 1, b - a - 1);
        if (m_str == L"random") {
            c.mode = MatchMode::RandomClass;
            c.field = RuleField::Class;
            c.pattern.clear();
            return true;
        }
        if (m_str == L"exact") c.mode = MatchMode::Exact;
        else if (m_str == L"wildcard") c.mode = MatchMode::Wildcard;
        else if (m_str == L"contains") c.mode = MatchMode::Contains;
        else c.mode = MatchMode::Exact;   // 未知 mode：取最窄，不放宽为 contains
        c.pattern = Lower(seg.substr(b + 1));
        return !c.pattern.empty();
    }

    // 行格式：B|W:cond[+cond...]；遗留裸格式 field:pattern（单条件、黑名单）
    inline bool ParseRuleLine(std::wstring const& line, Rule& r)
    {
        r.conditions.clear();
        if (line.empty()) return false;

        std::wstring body;
        size_t p1 = line.find(L':');
        if (p1 == std::wstring::npos) return false;
        std::wstring first = line.substr(0, p1);
        if (first == L"B" || first == L"W") {
            r.isWhitelist = (first == L"W");
            body = line.substr(p1 + 1);
        }
        else {
            r.isWhitelist = false;
            body = line;
        }

        size_t start = 0;
        for (;;) {
            size_t plus = body.find(L'+', start);
            std::wstring seg = (plus == std::wstring::npos) ? body.substr(start) : body.substr(start, plus - start);
            RuleCondition c;
            if (!ParseCondition(seg, c)) return false;
            r.conditions.push_back(std::move(c));
            if (plus == std::wstring::npos) break;
            start = plus + 1;
        }
        return !r.conditions.empty();
    }

    // 解析单个条件 JSON；RandomClass 无 pattern
    inline bool ParseRuleConditionJson(nlohmann::json const& cj, RuleCondition& c)
    {
        std::string f = cj.value("field", "exe");
        if (f == "exe") c.field = RuleField::Exe;
        else if (f == "path") c.field = RuleField::Path;
        else if (f == "title") c.field = RuleField::Title;
        else if (f == "class") c.field = RuleField::Class;
        else return false;

        std::string m = cj.value("mode", "contains");
        if (m == "random") {
            c.mode = MatchMode::RandomClass;
            c.field = RuleField::Class;
            c.pattern.clear();
            return true;
        }
        if (m == "exact") c.mode = MatchMode::Exact;
        else if (m == "wildcard") c.mode = MatchMode::Wildcard;
        else if (m == "contains") c.mode = MatchMode::Contains;
        else c.mode = MatchMode::Exact;   // 未知 mode：取最窄
        c.pattern = Lower(Utf8ToWString(cj.value("pattern", "")));
        return !c.pattern.empty();
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
                r.fromCommunity = item.value("source", "") == "community";

                if (item.contains("conditions") && item["conditions"].is_array()) {
                    for (auto& cj : item["conditions"]) {
                        RuleCondition c;
                        if (ParseRuleConditionJson(cj, c)) r.conditions.push_back(std::move(c));
                    }
                }
                else {
                    RuleCondition c;
                    if (ParseRuleConditionJson(item, c)) r.conditions.push_back(std::move(c));
                }
                if (!r.conditions.empty()) out.push_back(std::move(r));
            }
            return true;
        }
        catch (...) { return false; }
    }

    // 解析失败时把损坏的 rules.json 备份为 .bak，避免后续保存静默覆盖导致规则永久丢失
    inline void BackupCorruptRules()
    {
        std::wstring src = RulesPath();
        ::CopyFileW(src.c_str(), (src + L".bak").c_str(), FALSE);
    }

    inline bool LoadRulesJson(std::vector<Rule>& out, std::vector<std::wstring>& removedOut)
    {
        std::string utf8_text;
        if (!ReadFileToUtf8String(RulesPath(), utf8_text)) return false;
        if (utf8_text.empty()) return false;
        try {
            auto j = nlohmann::json::parse(utf8_text);
            if (!ParseRulesFromJsonString(utf8_text, out)) { BackupCorruptRules(); return false; }
            if (j.contains("communityRemoved") && j["communityRemoved"].is_array()) {
                for (auto& s : j["communityRemoved"])
                    removedOut.push_back(Utf8ToWString(s.get<std::string>()));
            }
            return true;
        }
        catch (...) { BackupCorruptRules(); return false; }
    }

    inline std::string SerializeRules(std::vector<Rule> const& rules, std::vector<std::wstring> const& removed)
    {
        nlohmann::json j;
        j["version"] = 1;
        j["rules"] = nlohmann::json::array();

        for (auto const& r : rules) {
            nlohmann::json item;
            item["list"] = r.isWhitelist ? "W" : "B";

            if (r.conditions.size() <= 1) {
                // 单条件写平铺字段（保持旧格式可读）；空条件按默认 exe/contains 占位
                RuleCondition c = r.conditions.empty() ? RuleCondition{} : r.conditions[0];
                item["field"] = FieldNameA(c.field);
                item["mode"] = ModeNameA(c.mode);
                item["pattern"] = (c.mode == MatchMode::RandomClass) ? "" : WStringToUtf8(c.pattern);
            }
            else {
                item["conditions"] = nlohmann::json::array();
                for (auto const& c : r.conditions) {
                    nlohmann::json cj;
                    cj["field"] = FieldNameA(c.field);
                    cj["mode"] = ModeNameA(c.mode);
                    if (c.mode != MatchMode::RandomClass) cj["pattern"] = WStringToUtf8(c.pattern);
                    item["conditions"].push_back(std::move(cj));
                }
            }
            if (r.fromCommunity) item["source"] = "community";

            j["rules"].push_back(std::move(item));
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