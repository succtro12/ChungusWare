#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "user_settings.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
namespace chungus_ui {
static bool valid(const UserSettings& s){
    return (s.resolution==-1||s.resolution==0||s.resolution==2||s.resolution==3||s.resolution==100||s.resolution==101)
        &&s.renderer>=0&&s.renderer<=2&&s.domain>=0&&s.domain<=2&&s.fg>=0&&s.fg<=5
        &&s.pacing>=0&&s.pacing<=2&&std::isfinite(s.target)&&s.target>=0&&s.target<=1000;
}
bool parseSettings(const std::string& text,UserSettings& output){
    if(text.size()>4096)return false;
    std::map<std::string,std::string> fields;std::istringstream stream(text);std::string line;
    while(std::getline(stream,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        auto eq=line.find('=');if(eq==std::string::npos||!fields.emplace(line.substr(0,eq),line.substr(eq+1)).second)return false;
    }
    auto number=[&](const char* key,auto& value){auto found=fields.find(key);if(found==fields.end())return false;const auto& v=found->second;auto r=std::from_chars(v.data(),v.data()+v.size(),value);return r.ec==std::errc{}&&r.ptr==v.data()+v.size();};
    int version=0,remember=0;if(!number("version",version)||version!=1||!number("remember",remember)||(remember!=0&&remember!=1))return false;
    UserSettings s;s.remember=remember!=0;
    if(!s.remember){if(fields.size()!=2)return false;output=s;return true;}
    if(fields.size()!=8||!number("resolution",s.resolution)||!number("renderer",s.renderer)||!number("domain",s.domain)
        ||!number("fg",s.fg)||!number("pacing",s.pacing)||!number("target",s.target)
        ||!valid(s))return false;
    output=s;return true;
}
std::string serializeSettings(const UserSettings& s){
    if(!valid(s))return {};
    std::ostringstream out;out<<"version=1\nremember="<<s.remember<<'\n';
    if(s.remember)out<<"resolution="<<s.resolution<<"\nrenderer="<<s.renderer<<"\ndomain="<<s.domain<<"\nfg="<<s.fg<<"\npacing="<<s.pacing<<"\ntarget="<<s.target<<'\n';
    return out.str();
}
bool loadSettings(const std::filesystem::path& path,UserSettings& s){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;
    auto size=file.tellg();if(size<0||size>4096)return false;
    std::string text(static_cast<size_t>(size),'\0');file.seekg(0);file.read(text.data(),static_cast<std::streamsize>(text.size()));
    return bool(file)&&parseSettings(text,s);
}
bool saveSettings(const std::filesystem::path& path,const UserSettings& s){
    auto text=serializeSettings(s);if(text.empty()||path.empty())return false;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
    auto temporary=path;temporary+=L".tmp";
    {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file)return false;file.write(text.data(),text.size());file.flush();if(!file)return false;}
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
}
