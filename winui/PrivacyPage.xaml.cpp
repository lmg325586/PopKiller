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
PopKiller 隐私声明
适用版本：Beta 0.8 ｜ 生效日期：2026 年 9 月 25 日

PopKiller 完全在本机运行，默认不向互联网发送任何数据。

一、本地数据
为识别弹窗，运行时会读取窗口的进程名、路径、标题、类名、样式、进程签名状态与创建时间，以及弹窗与鼠标光标的相对距离，并由本机启发式与模型计算特征评分。这些信息仅写入程序所在目录的本地文件，不会自动上传：
· blocklog.txt —— 拦截 / 放行 / 监控日志
· rules.json —— 黑名单 / 白名单与规则
· labels.json —— 日志页标注缓存
· winui.ini —— 程序设置
机器学习推理全部在本机离线完成。

二、网络请求
联网仅用于两处下载，均来自 raw.githubusercontent.com：①「社区规则库」开启时下载社区规则及其 SHA-256 校验文件；②打开「设置」页时检查 ML 模型更新（下载 popup_models.json），确认更新后再下载模型文件并校验 SHA-256。这些请求会向托管方暴露你的公网 IP、请求时间与 User-Agent，但不含任何本地数据。不打开设置页、不使用社区规则时，运行期间不会因此联网。

三、不收集
无遥测、统计或崩溃上报，无需账号；不读取文件内容、剪贴板、浏览历史或键盘输入。

四、你的控制
可随时关闭弹窗拦截或社区规则，并自行删除上述本地文件。日志满约 1 MB 会自动截断。)");
    }

    void PrivacyPage::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (Frame().CanGoBack())
        {
            Frame().GoBack();
        }
    }
}
