#include "phone_mirror/capture.hpp"
#include <sstream>
#include <iostream>
namespace {
void check(bool v) { if(!v) throw std::runtime_error("assertion failed"); }
template<class F> void reject(F f) {
    bool failed=false; try { f(); } catch(const pm::ProtocolError&) { failed=true; } check(failed);
}
pm::Message config() {
    pm::Message m; m.header.type=pm::Type::video_config; m.header.stream_id=1; m.header.stream_epoch=1;
    m.payload={0,0,2,64,0,0,5,0,0,0,0,1,0,0,0,5,0,0,0,5,0,0,0,1,0x67,0,0,0,1,0x68};
    m.header.payload_length=static_cast<std::uint32_t>(m.payload.size()); return m;
}
pm::Message frame() {
    pm::Message m; m.header.type=pm::Type::video_access_unit; m.header.stream_id=1; m.header.stream_epoch=1;
    m.header.sequence=1; m.header.flags=1; m.header.timestamp_us=1000;
    m.payload={0,0,0,1,0x65,0x88}; m.header.payload_length=6; return m;
}
}
int main() {
    try {
        auto c=config(), f=frame(); pm::CaptureValidator valid;
        valid.accept(c); valid.accept(f); valid.finish();
        check(valid.config().width==576 && valid.config().height==1280 && valid.frames()==1);
        reject([&] { pm::CaptureValidator v; v.accept(f); });
        reject([&] { pm::CaptureValidator v; v.accept(c); v.finish(); });
        for(const auto offset : {0,4,12,16,24,29}) {
            auto bad=c; bad.payload[offset]=255;
            reject([&] { pm::CaptureValidator v; v.accept(bad); });
        }
        reject([&] { auto bad=f; bad.header.sequence=2; pm::CaptureValidator v; v.accept(c); v.accept(bad); });
        reject([&] { auto bad=f; bad.header.stream_epoch=2; pm::CaptureValidator v; v.accept(c); v.accept(bad); });
        reject([&] { auto bad=f; bad.header.flags=0; pm::CaptureValidator v; v.accept(c); v.accept(bad); });
        reject([&] { auto bad=f; bad.payload[4]=0x61; pm::CaptureValidator v; v.accept(c); v.accept(bad); });
        reject([&] { auto bad=f; bad.header.sequence=2; bad.header.timestamp_us=999; pm::CaptureValidator v; v.accept(c); v.accept(f); v.accept(bad); });
        reject([&] { auto bad=f; bad.header.sequence=2; bad.header.timestamp_us=3601000000ull; pm::CaptureValidator v; v.accept(c); v.accept(f); v.accept(bad); });
        auto wire=pm::encode(c); const std::string data(wire.begin(),wire.end());
        std::istringstream full(data); check(pm::read_message(full).has_value()); check(!pm::read_message(full));
        for(std::size_t i=1;i<data.size();++i) {
            std::istringstream truncated(data.substr(0,i)); reject([&] { pm::read_message(truncated); });
        }
        std::cout << "Capture semantics checks passed.\n"; return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
