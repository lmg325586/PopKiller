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
    };

    inline std::wstring Lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }
}