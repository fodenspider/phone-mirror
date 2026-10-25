#include "phone_mirror/gesture.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

void check(bool value) { if(!value) throw std::runtime_error("gesture contract failed"); }
int main() {
    try {
        check(!pm::map_pointer(10,10,1000,1000,576,1280)); // horizontal black bar
        auto center=pm::map_pointer(500,500,1000,1000,576,1280);
        check(center && center->x>32000 && center->x<33500 && center->y>32000 && center->y<33500);
        check(!pm::map_pointer(500,500,0,1000,576,1280));
        const auto first=pm::map_pointer(0,0,576,1280,576,1280);
        const auto last=pm::map_pointer(575,1279,576,1280,576,1280);
        check(first && first->x==0 && first->y==0 && last && last->x==65535 && last->y==65535);
        check(!pm::map_pointer(576,1280,576,1280,576,1280));
        pm::WheelControl wheel;
        check(!wheel.update(30,1000)); check(!wheel.update(30,1010)); check(!wheel.update(30,1020));
        auto scroll=wheel.update(30,1030);
        check(scroll && scroll->start.y<scroll->end.y && scroll->end.y-scroll->start.y<8000);
        check(!wheel.update(-120,1100));
        scroll=wheel.update(-120,1210); check(scroll && scroll->start.y>scroll->end.y);
        check(!wheel.update(0,1500));
        check(!wheel.update(60,2000)); scroll=wheel.update(60,2010); check(scroll.has_value());
        wheel.reset(); scroll=wheel.update(-1200,1500);
        check(scroll && scroll->start.y-scroll->end.y<8000);
        auto message=pm::gesture_message({2,{1000,2000},{3000,4000},180},7);
        std::ifstream file(std::filesystem::path(u8"" PM_FIXTURE_DIR)/"gesture.hex"); std::string hex; file>>hex;
        if(!file) throw std::runtime_error("cannot read gesture fixture");
        std::vector<std::uint8_t> expected;
        for(std::size_t i=0;i<hex.size();i+=2) expected.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
        check(pm::encode(message)==expected);
        bool rejected=false; try { pm::gesture_message({1,{0,0},{1,0},50},0); } catch(const pm::ProtocolError&) { rejected=true; }
        check(rejected);
        std::cout<<"Gesture coordinates and wire contract passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
