#pragma once
#include <filesystem>
#include <string>
namespace chungus_ui {
struct UserSettings {
    bool remember=false;
    int resolution=-1,renderer=0,domain=2,fg=0,pacing=0;
    float target=0;
};
bool parseSettings(const std::string&,UserSettings&);
std::string serializeSettings(const UserSettings&);
bool loadSettings(const std::filesystem::path&,UserSettings&);
bool saveSettings(const std::filesystem::path&,const UserSettings&);
}
