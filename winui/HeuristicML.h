#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include "HeuristicScorer.h"

namespace HeuristicML
{
    // ML 特征维度（必须与 ML/train.py 的 FEATURE_NAMES 一致）
    inline constexpr size_t kFeatureCount = 27;

    // 特征下标（顺序必须与 ML/train.py 的 FEATURE_NAMES 一致）
    enum : size_t {
        kOwner = 0,
        kToolWin = 1,
        kTopmost = 2,
        kNoActivate = 3,
        kResizable = 4,
        kHasMinMax = 5,
        kCaptionSysmenu = 6,
        kTitleEmpty = 7,
        kSmallWindow = 8,
        kLargeWindow = 9,
        kPathTemp = 10,
        kPathRoaming = 11,
        kClsHex = 12,
        kYoungProcess = 13,
        kUnsigned = 14,
        kUserIdle = 15,
        kFarFromMouse = 16,
        kTitleLen = 17,
        kTitleKwHits = 18,
        kGoodExe = 19,
        kWidgetWin = 20,
        kDialog32770 = 21,
        kExeDigitRatio = 22,
        kParentExplorer = 23,
        kParentSystem = 24,
        kParentUnknown = 25,
        kSameProcPrevFg = 26,
    };

    inline constexpr size_t kTensorRank = 2;   // 输入张量 rank（batch × feature）

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

    // ONNX 静态 ML 引擎：持有 rf/lr 两个会话；未加载（缺模型、维度不符、加载异常）即禁用 ML。
    struct MLEngine {
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::Session> sessionRf;
        std::unique_ptr<Ort::Session> sessionLr;
        bool m_warned = false;

        void WarnOnce(const wchar_t* msg) {
            if (m_warned) return;
            m_warned = true;
            ::OutputDebugStringW(msg);
            ::OutputDebugStringW(L"\n");
        }

        // 从 StaticML 加载 popup_rf.onnx / popup_lr.onnx 并校验输入维度为 kFeatureCount（27）；失败则告警并禁用 ML。
        bool Init() {
            if (sessionRf || sessionLr) return true;

            const OrtApiBase* base = OrtGetApiBase();
            if (!base || !base->GetApi(ORT_API_VERSION)) {
                WarnOnce(L"ONNX Runtime 不可用或与编译头文件版本不匹配，\n静态ML启发已禁用请重新下载");
                return false;
            }

            if (!env) env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "PopKillerML");

            std::wstring dir = GetSelfPath();
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

                // 模型输入维度必须与 kFeatureCount 一致，否则禁用（避免形状不匹配静默失败）
                auto shape = sessionRf->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
                if (shape.size() != kTensorRank || shape[1] != static_cast<int64_t>(kFeatureCount)) {
                    sessionRf.reset();
                    sessionLr.reset();
                    WarnOnce(L"ML 模型特征维度与当前版本不一致，请重新训练/更新模型；静态ML启发已禁用。");
                    return false;
                }
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

        // 单模型推理：输入 27 维特征，输出该模型 label==1 的判定（异常按 false 处理）。
        bool RunSession(Ort::Session* session, const std::array<float, kFeatureCount>& features) {
            try {
                auto inAlloc = session->GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                auto outAlloc = session->GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                const char* inputName = inAlloc.get();
                const char* outputName = outAlloc.get();

                Ort::MemoryInfo info("Cpu", OrtDeviceAllocator, 0, OrtMemTypeDefault);
                auto inputTensor = Ort::Value::CreateTensor<float>(
                    info, const_cast<float*>(features.data()), kFeatureCount,
                    std::array<int64_t, 2>{1, static_cast<int64_t>(kFeatureCount)}.data(), kTensorRank);

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

        // 组装 27 维特征（下标契约见上方 enum）交 rf、lr 两模型推理，二者都为 1 才返回 true；未加载则返回 false。
        bool Predict(HWND hwnd, DWORD evTime, DWORD prevForegroundPid = 0) {
            if (!sessionRf || !sessionLr) return false;

            HeuristicScorer::Features f = HeuristicScorer::ExtractFeatures(hwnd, evTime, prevForegroundPid);
            std::wstring title = GetTitle(hwnd);
            std::wstring cls = GetClass(hwnd);
            std::wstring exe = GetProcessName(hwnd);

            std::array<float, kFeatureCount> features = { 0 };

            features[kOwner] = (f.hasOwner > 0) ? 1.0f : 0.0f;
            features[kToolWin] = (f.toolWin > 0) ? 1.0f : 0.0f;
            features[kTopmost] = (f.topmost > 0) ? 1.0f : 0.0f;
            features[kNoActivate] = (f.noActivate > 0) ? 1.0f : 0.0f;
            features[kResizable] = (f.resizable > 0) ? 1.0f : 0.0f;
            features[kHasMinMax] = (f.hasMinMax > 0) ? 1.0f : 0.0f;
            features[kCaptionSysmenu] = (f.captionSysmenu > 0) ? 1.0f : 0.0f;
            features[kTitleEmpty] = (f.titleEmpty > 0) ? 1.0f : 0.0f;

            // 尺寸用 DPI 归一的逻辑像素（二值特征，语义等价，无需重训）
            features[kSmallWindow] = (f.wDip < HeuristicScorer::kSmallW && f.hDip < HeuristicScorer::kSmallH) ? 1.0f : 0.0f;
            features[kLargeWindow] = (f.wDip > HeuristicScorer::kLargeW || f.hDip > HeuristicScorer::kLargeH) ? 1.0f : 0.0f;
            features[kPathTemp] = (f.pathTemp > 0) ? 1.0f : 0.0f;
            features[kPathRoaming] = (f.pathRoaming > 0) ? 1.0f : 0.0f;
            features[kClsHex] = (f.clsHexRatio > HeuristicScorer::kHexRatioThreshold) ? 1.0f : 0.0f;
            features[kYoungProcess] = (f.procAgeSec >= 0 && f.procAgeSec < HeuristicScorer::kYoungProcessSec) ? 1.0f : 0.0f;
            features[kUnsigned] = (!f.path.empty() && !HeuristicScorer::IsFileSignedCached(f.path)) ? 1.0f : 0.0f;

            features[kUserIdle] = f.userIdle;
            features[kFarFromMouse] = f.farFromMouse;

            features[kTitleLen] = static_cast<float>(title.size());
            float kw_hits = 0.0f;
            for (const auto& kw : AD_KEYWORDS) {
                if (title.find(kw) != std::wstring::npos) kw_hits += 1.0f;
            }
            features[kTitleKwHits] = kw_hits;
            features[kGoodExe] = (std::find(GOOD_EXES.begin(), GOOD_EXES.end(), exe) != GOOD_EXES.end()) ? 1.0f : 0.0f;
            features[kWidgetWin] = (cls.find(L"widgetwin") != std::wstring::npos) ? 1.0f : 0.0f;
            features[kDialog32770] = (cls == L"#32770") ? 1.0f : 0.0f;
            int digits = 0;
            for (wchar_t c : exe) if (c >= L'0' && c <= L'9') digits++;
            features[kExeDigitRatio] = float(digits) / float(std::max<size_t>(1, exe.size()));

            // 新增 4 维：父进程（启动者）类别 + 与刚在前台进程同进程
            features[kParentExplorer] = f.parentExplorer;
            features[kParentSystem] = f.parentSystem;
            features[kParentUnknown] = f.parentUnknown;
            features[kSameProcPrevFg] = f.sameProcAsPrevForeground;

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