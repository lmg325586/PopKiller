#include "pch.h"
#include "PrivacyPage.xaml.h"
#if __has_include("PrivacyPage.g.cpp")
#include "PrivacyPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::winui::implementation
{
    PrivacyPage::PrivacyPage()
    {
        InitializeComponent();

        PrivacyText().Text(LR"(
隐私声明
最后更新：2026-09-25

PopKiller 是一款在本地运行的弹窗拦截工具。我们遵循“本地优先、最小必要”的原则处理数据。

1. 不收集个人信息
本应用无需注册账号，不收集、不上传任何可识别个人身份的信息，也不包含遥测或使用统计。

2. 本地保存的数据
以下数据仅保存在本机（winui.exe 同目录），不会自动上传：
  · 拦截日志：blocklog.txt
  · 拦截规则：rules.json
  · 训练样本标签：labels.json
  · 自启动调试日志：autostart_debug.log

3. 网络请求
仅当你启用“社区规则”时，本应用会从配置的服务器下载社区规则文件及其 SHA256 校验文件，用于更新拦截规则。该请求不携带你的日志、样本或任何个人数据。

4. 第三方组件
本应用内置并在本机运行 Microsoft Windows App SDK、ONNX Runtime 等组件，不会将你的数据交由第三方处理。

5. 你的控制
你可以随时在设置中关闭弹窗拦截或社区规则，并可自行删除 winui.exe 同目录下的上述本地文件。)");
    }

    void PrivacyPage::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (Frame().CanGoBack())
        {
            Frame().GoBack();
        }
    }
}
