#pragma once

#include <string>

namespace zb {

// ---- 界面语言与设置 --------------------------------------------------------
// 设置界面本身是自研渲染器画出来的内置页（browser://settings），
// 这里只负责"当前设置是什么"和"界面字符串取哪一份"。

// Auto 表示跟随系统 UI 语言。
enum class Lang { Auto, Zh, En };

// 设置项。目前只有界面语言；以后要加别的（主页、代理…）往这里加字段即可。
struct Settings {
    Lang lang = Lang::Auto;
};

Settings& MutableSettings();
const Settings& CurrentSettings();

// 设置文件路径。默认 %APPDATA%\ZeroBrowser\settings.ini。
// 环境变量 ZB_SETTINGS 可以覆盖它 —— 自动化测试用这个把设置写进临时文件，
// 不去动用户真实的配置。
std::string SettingsFilePath();

// 读设置。文件不存在或读不出来时保持默认值，不报错、不打断启动。
void LoadSettings();
// 写设置。返回是否写成功；失败不算致命错误（只是这次选择留不下来）。
bool SaveSettings();

// 实际生效的语言（Auto 按系统 UI 语言落定为 Zh 或 En）。
bool UiIsEnglish();
// "zh" / "en"
const char* UiLangCode();
// 当前设置里语言这一项的原始取值："auto" / "zh" / "en"
const char* UiLangSetting();

// 界面字符串。找不到 key 时**返回 key 本身**：漏翻的地方会以键名露出来，
// 而不是静默变成空白 —— 空白在截图回归里是看不出来的。
const char* T(const char* key);

// "zh" / "en" / "auto" -> Lang；不认识时返回 fallback。
Lang ParseLang(const std::string& s, Lang fallback);
const char* LangToCode(Lang lang);

}  // namespace zb
