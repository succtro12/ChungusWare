#include "user_settings.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace chungus_ui;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)throw std::runtime_error("provide a project-local test directory");
    auto path=std::filesystem::path(argv[1])/L"settings.ini";UserSettings wanted;wanted.remember=true;wanted.resolution=100;wanted.renderer=2;wanted.domain=2;wanted.fg=3;wanted.pacing=1;wanted.target=177.5f;
    if(!saveSettings(path,wanted))throw std::runtime_error("save failed");
    UserSettings loaded;if(!loadSettings(path,loaded)||serializeSettings(loaded)!=serializeSettings(wanted))throw std::runtime_error("restart round-trip lost settings");
    wanted.resolution=101;wanted.fg=5;if(!saveSettings(path,wanted)||!loadSettings(path,loaded)||loaded.resolution!=101||loaded.fg!=5)throw std::runtime_error("atomic replacement failed");
    wanted.remember=false;if(!saveSettings(path,wanted)||!loadSettings(path,loaded)||loaded.remember||loaded.fg!=0||loaded.resolution!=-1)throw std::runtime_error("disable did not discard stored preferences");
    UserSettings unchanged;unchanged.resolution=3;
    for(auto invalid:{std::string("version=9\nremember=1\n"),std::string("version=1\nremember=1\nfg=999\n"),std::string("version=1\nremember=0\nremember=1\n"),std::string(5000,'x')})
        if(parseSettings(invalid,unchanged)||unchanged.resolution!=3)throw std::runtime_error("bad config accepted or partially applied");
    wanted.remember=true;auto text=serializeSettings(wanted);text.replace(text.find("target="),text.find('\n',text.find("target="))-text.find("target="),"target=nan");if(parseSettings(text,loaded))throw std::runtime_error("nonfinite target accepted");
    text=serializeSettings(wanted);text.replace(text.find("fg="),text.find('\n',text.find("fg="))-text.find("fg="),"fg=999");if(parseSettings(text,loaded))throw std::runtime_error("unsupported FG count accepted");
    text=serializeSettings(wanted);text.replace(text.find("resolution="),text.find('\n',text.find("resolution="))-text.find("resolution="),"resolution=102");if(parseSettings(text,loaded))throw std::runtime_error("removed custom resolution accepted");
    std::cout<<"Settings checks passed: restart round-trip, atomic overwrite, opt-out, bounded strict validation.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
