#pragma once
#include <string>
#include <algorithm>

namespace PopupBlocker
{
    enum class RuleField { Exe, Path, Title, Class };
    enum class MatchMode { Contains, Exact, Wildcard };

    struct Rule
    {
        bool isWhitelist = false;
        RuleField field = RuleField::Exe;
        MatchMode mode = MatchMode::Contains;
        std::wstring pattern;
        bool fromCommunity = false;
    };

    inline std::wstring Lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    inline std::wstring RuleKey(Rule const& r)
    {
        std::wstring k = r.isWhitelist ? L"W|" : L"B|";
        switch (r.field) {
        case RuleField::Exe:   k += L"exe|";   break;
        case RuleField::Path:  k += L"path|";  break;
        case RuleField::Title: k += L"title|"; break;
        case RuleField::Class: k += L"class|"; break;
        }
        switch (r.mode) {
        case MatchMode::Contains: k += L"contains|"; break;
        case MatchMode::Exact:    k += L"exact|";    break;
        case MatchMode::Wildcard: k += L"wildcard|"; break;
        }
        k += r.pattern;
        return k;
    }
}