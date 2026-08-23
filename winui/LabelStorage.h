#pragma once
#include <windows.h>
#include <string>
#include <map>
#include "RuleStorage.h"

namespace SampleLabels
{
    struct Sample
    {
        std::wstring label;
        std::wstring line;
        std::wstring action;
        std::wstring reason;
        std::wstring title;
        std::wstring cls;
        std::wstring exe;
        std::wstring raw;
        int score = 0;
    };

    inline std::wstring LabelsPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        return p.substr(0, pos + 1) + L"labels.json";
    }

    inline std::wstring ExtractField(std::wstring const& s, std::wstring const& key)
    {
        auto pos = s.find(key);
        if (pos == std::wstring::npos) return {};
        pos += key.size();
        auto end = s.find(L" | ", pos);
        return s.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
    }

    inline Sample ParseLine(std::wstring const& line)
    {
        Sample s;
        s.line = line;
        s.action = ExtractField(line, L"action=");
        s.reason = ExtractField(line, L"reason=");
        s.title = ExtractField(line, L"title=");
        s.cls = ExtractField(line, L"class=");
        s.exe = ExtractField(line, L"exe=");

        auto hp = line.find(L"heuristic(");
        if (hp != std::wstring::npos)
        {
            hp += 10;
            auto he = line.find(L')', hp);
            if (he != std::wstring::npos)
                s.score = ::_wtoi(line.substr(hp, he - hp).c_str());
        }

        auto rp = line.find(L"raw=");
        if (rp != std::wstring::npos) {
            rp += 4;
            auto re = line.find(L' ', rp);
            s.raw = line.substr(rp, re == std::wstring::npos ? std::wstring::npos : re - rp);
        }
        else {
            s.raw = std::wstring(12, L'?');
        }

        return s;
    }

    inline void Load(std::map<std::wstring, Sample>& out)
    {
        out.clear();
        std::string utf8;
        if (!PopupBlocker::ReadFileToUtf8String(LabelsPath(), utf8) || utf8.empty()) return;
        try {
            auto j = nlohmann::json::parse(utf8);
            if (!j.contains("samples") || !j["samples"].is_array()) return;
            for (auto& it : j["samples"]) {
                Sample s;
                s.label = PopupBlocker::Utf8ToWString(it.value("label", ""));
                s.line = PopupBlocker::Utf8ToWString(it.value("line", ""));
                if (s.line.empty()) continue;
                s.action = PopupBlocker::Utf8ToWString(it.value("action", ""));
                s.reason = PopupBlocker::Utf8ToWString(it.value("reason", ""));
                s.title = PopupBlocker::Utf8ToWString(it.value("title", ""));
                s.cls = PopupBlocker::Utf8ToWString(it.value("class", ""));
                s.exe = PopupBlocker::Utf8ToWString(it.value("exe", ""));
                s.score = it.value("score", 0);
                s.raw = PopupBlocker::Utf8ToWString(it.value("raw", ""));
                out[s.line] = s;
            }
        }
        catch (...) {}
    }

    inline bool Save(std::map<std::wstring, Sample> const& m)
    {
        nlohmann::json j;
        j["version"] = 1;
        j["samples"] = nlohmann::json::array();
        for (auto const& [k, s] : m) {
            nlohmann::json it;
            it["label"] = PopupBlocker::WStringToUtf8(s.label);
            it["line"] = PopupBlocker::WStringToUtf8(s.line);
            it["action"] = PopupBlocker::WStringToUtf8(s.action);
            it["reason"] = PopupBlocker::WStringToUtf8(s.reason);
            it["title"] = PopupBlocker::WStringToUtf8(s.title);
            it["class"] = PopupBlocker::WStringToUtf8(s.cls);
            it["exe"] = PopupBlocker::WStringToUtf8(s.exe);
            it["score"] = s.score;
            it["raw"] = PopupBlocker::WStringToUtf8(s.raw);
            j["samples"].push_back(it);
        }
        return PopupBlocker::WriteUtf8StringToFile(LabelsPath(), j.dump(4));
    }

    inline std::string ExportJson(std::map<std::wstring, Sample> const& m)
    {
        nlohmann::json j;
        j["version"] = 1;
        j["type"] = "popkiller_training_samples";
        j["samples"] = nlohmann::json::array();
        for (auto const& [k, s] : m) {
            nlohmann::json it;
            it["label"] = PopupBlocker::WStringToUtf8(s.label);
            it["action"] = PopupBlocker::WStringToUtf8(s.action);
            it["reason"] = PopupBlocker::WStringToUtf8(s.reason);
            it["title"] = PopupBlocker::WStringToUtf8(s.title);
            it["class"] = PopupBlocker::WStringToUtf8(s.cls);
            it["exe"] = PopupBlocker::WStringToUtf8(s.exe);
            it["score"] = s.score;
            it["raw"] = PopupBlocker::WStringToUtf8(s.raw);
            j["samples"].push_back(it);
        }
        return j.dump(4);
    }
}