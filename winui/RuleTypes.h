#pragma once
#include <string>
#include <algorithm>
#include <vector>
#include <cwctype>

namespace PopupBlocker
{
    enum class RuleField { Exe, Path, Title, Class };
    enum class MatchMode { Contains, Exact, Wildcard, RandomClass };

    // 单条匹配条件；mode==RandomClass 时忽略 pattern（判定类名是否随机）
    struct RuleCondition
    {
        RuleField field = RuleField::Exe;
        MatchMode mode = MatchMode::Contains;
        std::wstring pattern;
    };

    // 规则 = 白/黑名单 + 若干条件（全部满足才命中，AND）；单条件即 conditions.size()==1
    struct Rule
    {
        bool isWhitelist = false;
        std::vector<RuleCondition> conditions;
        bool fromCommunity = false;
    };

    inline wchar_t const* FieldName(RuleField f)
    {
        switch (f) {
        case RuleField::Path:  return L"path";
        case RuleField::Title: return L"title";
        case RuleField::Class: return L"class";
        default:               return L"exe";
        }
    }
    inline wchar_t const* ModeName(MatchMode m)
    {
        switch (m) {
        case MatchMode::Exact:       return L"exact";
        case MatchMode::Wildcard:    return L"wildcard";
        case MatchMode::RandomClass: return L"random";
        default:                     return L"contains";
        }
    }
    inline char const* FieldNameA(RuleField f)
    {
        switch (f) {
        case RuleField::Path:  return "path";
        case RuleField::Title: return "title";
        case RuleField::Class: return "class";
        default:               return "exe";
        }
    }
    inline char const* ModeNameA(MatchMode m)
    {
        switch (m) {
        case MatchMode::Exact:       return "exact";
        case MatchMode::Wildcard:    return "wildcard";
        case MatchMode::RandomClass: return "random";
        default:                     return "contains";
        }
    }

    inline std::wstring Lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    // 单条件：与旧格式字节一致（W|field|mode|pattern），保证旧去重/墓碑兼容；
    // 多条件：各条件 key 排序后用 \x1F 连接（顺序无关）。
    inline std::wstring RuleKey(Rule const& r)
    {
        std::wstring k = r.isWhitelist ? L"W|" : L"B|";
        if (r.conditions.empty()) return k;

        auto condKey = [](RuleCondition const& c) -> std::wstring {
            if (c.mode == MatchMode::RandomClass) return L"class|random";
            return std::wstring(FieldName(c.field)) + L"|" + ModeName(c.mode) + L"|" + c.pattern;
            };

        if (r.conditions.size() == 1) return k + condKey(r.conditions[0]);

        std::vector<std::wstring> keys;
        keys.reserve(r.conditions.size());
        for (auto const& c : r.conditions) keys.push_back(condKey(c));
        std::sort(keys.begin(), keys.end());

        std::wstring joined;
        for (size_t i = 0; i < keys.size(); ++i) { if (i) joined += L'\x1F'; joined += keys[i]; }
        return k + joined;
    }
}