#pragma once

#include <string>

namespace zb {






enum class Lang { Auto, Zh, En };


struct Settings {
    Lang lang = Lang::Auto;
};

Settings& MutableSettings();
const Settings& CurrentSettings();




std::string SettingsFilePath();


void LoadSettings();

bool SaveSettings();


bool UiIsEnglish();

const char* UiLangCode();

const char* UiLangSetting();



const char* T(const char* key);


Lang ParseLang(const std::string& s, Lang fallback);
const char* LangToCode(Lang lang);

}  
