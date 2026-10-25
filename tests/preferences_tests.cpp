#include "preferences.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
int main() {
    auto root=std::filesystem::temp_directory_path()/std::filesystem::path("pm-prefs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto path=root/"settings.txt";
        if(pm::ui::read_preferences(path).software) throw std::runtime_error("missing defaults");
        pm::ui::Preferences p; p.software=true; p.width=900; p.height=700; p.guide_step=3; p.quality=2; p.wheel_percent=30;
        if(!pm::ui::write_preferences(path,p)) throw std::runtime_error("write failed");
        auto read=pm::ui::read_preferences(path);
        if(!read.software || read.width!=900 || read.height!=700 || read.guide_step!=3 || read.quality!=2 || read.wheel_percent!=30) throw std::runtime_error("roundtrip failed");
        p.software=false; if(!pm::ui::write_preferences(path,p) || pm::ui::read_preferences(path).software) throw std::runtime_error("replace failed");
        {std::ofstream file(path); file<<"software 2\nwidth -1\nheight 999999\nguide 4\nquality 99\nwheel 100\nwidth 1000 garbage\nunknown 1\n";}
        read=pm::ui::read_preferences(path);
        if(read.software || read.width!=720 || read.height!=820 || read.guide_step!=0 || read.quality!=1 || read.wheel_percent!=12) throw std::runtime_error("invalid input accepted");
        {std::ofstream file(path); file<<std::string(5000,'x');}
        if(pm::ui::read_preferences(path).width!=720) throw std::runtime_error("oversize settings accepted");
        for(bool components:{false,true}) for(bool apk:{false,true}) {
            const auto text=pm::ui::readiness(components,apk);
            if(text.find(L"连接手机后检查")==std::wstring::npos) throw std::runtime_error("readiness overclaims phone status");
        }
        std::filesystem::remove_all(root); std::cout<<"Settings recovery, replacement and readiness passed\n";
    } catch(const std::exception& e) { std::filesystem::remove_all(root); std::cerr<<e.what()<<'\n'; return 1; }
}
