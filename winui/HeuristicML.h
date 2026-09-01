#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include "HeuristicScorer.h"
#include "RuleTypes.h"

namespace HeuristicML
{
    inline const std::vector<std::wstring> AD_KEYWORDS = {
        L"广告", L"优惠", L"促销", L"免费", L"中奖", L"礼包",
        L"热点", L"速看", L"推荐", L"清理", L"加速", L"升级", L"弹窗", L"资讯",
    };
    inline const std::vector<std::wstring> GOOD_EXES = {
        L"devenv.exe", L"code.exe", L"chrome.exe", L"msedge.exe", L"firefox.exe",
        L"windowsterminal.exe", L"explorer.exe", L"wechat.exe", L"weixin.exe",
        L"qq.exe", L"dingtalk.exe", L"tim.exe", L"notepad.exe", L"notepad++.exe",
        L"everything.exe", L"snipaste.exe", L"listary.exe",
        L"steamwebhelper.exe", L"steam.exe", L"qbittorrent.exe", L"rvrvpngui.exe",
        L"mixline.exe", L"mixline.ui.exe", L"oopz.exe", L"translucenttb.exe", L"hyp.exe",
        L"svchost.exe",
    };

    inline std::wstring Lower(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    inline std::wstring GetTitle(HWND hwnd) { WCHAR buf[256]{}; ::GetWindowTextW(hwnd, buf, 256); return Lower(buf); }
    inline std::wstring GetClass(HWND hwnd) { WCHAR buf[256]{}; ::GetClassNameW(hwnd, buf, 256); return Lower(buf); }
    inline std::wstring GetProcessName(HWND hwnd)
    {
        DWORD pid{};
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return {};
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return {};
        WCHAR path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        std::wstring name;
        if (::QueryFullProcessImageNameW(h, 0, path, &size))
        {
            std::wstring p = path;
            auto pos = p.find_last_of(L"\\/");
            name = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
        }
        ::CloseHandle(h);
        return Lower(name);
    }

    struct MLEngine {
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::Session> sessionRf;
        std::unique_ptr<Ort::Session> sessionLr;
        bool m_warned = false;

        void WarnOnce(const wchar_t* msg) {
            if (m_warned) return;
            m_warned = true;
            MessageBoxW(nullptr, msg, L"PopKiller ML", MB_OK | MB_ICONWARNING);
        }

        bool Init() {
            if (sessionRf || sessionLr) return true;

            const OrtApiBase* base = OrtGetApiBase();
            if (!base || !base->GetApi(ORT_API_VERSION)) {
                WarnOnce(L"ONNX Runtime 不可用或与编译头文件版本不匹配，\n静态ML启发已禁用请重新下载");
                return false;
            }

            if (!env) env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "PopKillerML");

            WCHAR path[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring dir(path);
            auto pos = dir.find_last_of(L"\\/");
            dir = dir.substr(0, pos + 1) + L"StaticML\\";

            std::wstring rfPath = dir + L"popup_rf.onnx";
            std::wstring lrPath = dir + L"popup_lr.onnx";
            if (::GetFileAttributesW(rfPath.c_str()) == INVALID_FILE_ATTRIBUTES ||
                ::GetFileAttributesW(lrPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring msg = L"ML 模型文件缺失，静态ML启发已禁用：\n" + rfPath + L"\n" + lrPath;
                WarnOnce(msg.c_str());
                return false;
            }

            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            try {
                sessionRf = std::make_unique<Ort::Session>(*env, rfPath.c_str(), opts);
                sessionLr = std::make_unique<Ort::Session>(*env, lrPath.c_str(), opts);
                m_warned = false;
                return true;
            }
            catch (...) {
                sessionRf.reset();
                sessionLr.reset();
                WarnOnce(L"ML 模型加载异常，静态ML启发已禁用。");
                return false;
            }
        }

        bool RunSession(Ort::Session* session, const std::array<float, 23>& features) {
            try {
                auto inAlloc = session->GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                auto outAlloc = session->GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                const char* inputName = inAlloc.get();
                const char* outputName = outAlloc.get();

                Ort::MemoryInfo info("Cpu", OrtDeviceAllocator, 0, OrtMemTypeDefault);
                auto inputTensor = Ort::Value::CreateTensor<float>(
                    info, const_cast<float*>(features.data()), 23,
                    std::array<int64_t, 2>{1, 23}.data(), 2);

                auto outputTensors = session->Run(
                    Ort::RunOptions{ nullptr },
                    &inputName, &inputTensor, 1,
                    &outputName, 1);

                auto labelTensor = outputTensors.front().GetTensorData<int64_t>();
                return labelTensor[0] == 1;
            }
            catch (...) {
                return false;
            }
        }

        bool Predict(HWND hwnd, DWORD evTime) {
            if (!sessionRf || !sessionLr) return false;

            HeuristicScorer::Features f = HeuristicScorer::ExtractFeatures(hwnd, evTime);
            std::wstring title = GetTitle(hwnd);
            std::wstring cls = GetClass(hwnd);
            std::wstring exe = GetProcessName(hwnd);

            std::array<float, 23> features = { 0 };

            features[0] = (f.hasOwner > 0) ? 1.0f : 0.0f;
            features[1] = (f.toolWin > 0) ? 1.0f : 0.0f;
            features[2] = (f.topmost > 0) ? 1.0f : 0.0f;
            features[3] = (f.noActivate > 0) ? 1.0f : 0.0f;
            features[4] = (f.resizable > 0) ? 1.0f : 0.0f;
            features[5] = (f.hasMinMax > 0) ? 1.0f : 0.0f;
            features[6] = (f.captionSysmenu > 0) ? 1.0f : 0.0f;
            features[7] = (f.titleEmpty > 0) ? 1.0f : 0.0f;

            RECT rc{}; ::GetWindowRect(hwnd, &rc);
            float wPx = float(rc.right - rc.left);
            float hPx = float(rc.bottom - rc.top);
            features[8] = (wPx < 400 && hPx < 300) ? 1.0f : 0.0f;
            features[9] = (wPx > 800 || hPx > 600) ? 1.0f : 0.0f;
            features[10] = (f.pathTemp > 0) ? 1.0f : 0.0f;
            features[11] = (f.pathRoaming > 0) ? 1.0f : 0.0f;
            features[12] = (f.clsHexRatio > 0.8f) ? 1.0f : 0.0f;
            features[13] = (f.procAgeSec >= 0 && f.procAgeSec < 120) ? 1.0f : 0.0f;
            features[14] = (!f.path.empty() && !HeuristicScorer::IsFileSignedCached(f.path)) ? 1.0f : 0.0f;

            features[15] = f.userIdle;
            features[16] = f.farFromMouse;

            features[17] = static_cast<float>(title.size());
            float kw_hits = 0.0f;
            for (const auto& kw : AD_KEYWORDS) {
                if (title.find(kw) != std::wstring::npos) kw_hits += 1.0f;
            }
            features[18] = kw_hits;
            features[19] = (std::find(GOOD_EXES.begin(), GOOD_EXES.end(), exe) != GOOD_EXES.end()) ? 1.0f : 0.0f;
            features[20] = (cls.find(L"widgetwin") != std::wstring::npos) ? 1.0f : 0.0f;
            features[21] = (cls == L"#32770") ? 1.0f : 0.0f;
            int digits = 0;
            for (wchar_t c : exe) if (c >= L'0' && c <= L'9') digits++;
            features[22] = float(digits) / float(std::max<size_t>(1, exe.size()));

            bool rf_pred = RunSession(sessionRf.get(), features);
            bool lr_pred = RunSession(sessionLr.get(), features);

            return rf_pred && lr_pred;
        }
    };

    inline MLEngine& GetInstance() {
        static MLEngine instance;
        return instance;
    }
}