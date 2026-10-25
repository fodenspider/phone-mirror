#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
namespace pm::ui {
struct Preferences {
    bool software=false;
    int width=720,height=820;
    unsigned guide_step=0;
    int quality=1;
    int wheel_percent=12;
};
inline Preferences read_preferences(const std::filesystem::path& path) {
    Preferences p;
    std::error_code ec;
    if(std::filesystem::file_size(path,ec)>4096 || ec) return p;
    std::ifstream file(path); std::string line;
    while(std::getline(file,line)) {
        std::istringstream row(line); std::string key,extra; int value;
        if(!(row>>key>>value) || row>>extra) continue;
        if(key=="software" && (value==0 || value==1)) p.software=value==1;
        if(key=="width" && value>=520 && value<=2400) p.width=value;
        if(key=="height" && value>=420 && value<=1800) p.height=value;
        if(key=="guide" && value>=0 && value<4) p.guide_step=static_cast<unsigned>(value);
        if(key=="wheel" && (value==6 || value==12 || value==20 || value==30)) p.wheel_percent=value;
        if(key=="quality" && value>=0 && value<3) p.quality=value;
    }
    return p;
}
inline bool write_preferences(const std::filesystem::path& path,const Preferences& p) {
    if(path.empty()) return false;
    std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec);
    if(ec) return false;
    auto temporary=path; temporary+=L".tmp";
    { std::ofstream file(temporary,std::ios::trunc);
      file<<"software "<<p.software<<"\nwidth "<<p.width<<"\nheight "<<p.height<<"\nguide "<<p.guide_step<<"\nquality "<<p.quality<<"\nwheel "<<p.wheel_percent<<'\n';
      file.flush(); if(!file) return false; }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
#else
    std::filesystem::rename(temporary,path,ec); return !ec;
#endif
}
inline std::wstring readiness(bool components,bool companion) {
    return std::wstring(L"电脑端准备情况\r\n\r\n连接组件：")+(components?L"已找到":L"待导入，请完成指南第一步")+
        L"\r\n手机安装包："+(companion?L"已备好，可按指南安装":L"缺失，请使用完整安装包或便携包")+
        L"\r\n\r\n手机连接、应用安装状态与权限：连接手机后检查。\r\n此检查仅查看电脑文件，不连接设备。";
}
}
