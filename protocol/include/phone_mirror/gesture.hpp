#pragma once
#include "protocol.hpp"
#include <optional>
#include <algorithm>

namespace pm {
struct Point { std::uint32_t x=0,y=0; };
struct Gesture { std::uint32_t action=1; Point start,end; std::uint32_t duration_ms=50; };
// A short stationary click never enters a held-touch session.
inline bool begins_continuous_touch(Point start,Point current,std::uint64_t held_ms) {
    const auto dx=start.x>current.x?start.x-current.x:current.x-start.x;
    const auto dy=start.y>current.y?start.y-current.y:current.y-start.y;
    return dx>=500 || dy>=500 || held_ms>=500;
}
// Accumulate high-resolution wheel deltas; a notch moves a bounded fraction of the screen.
// Limit bursts rather than replaying a delayed queue after scrolling stops.
class WheelControl {
    int pending_=0;
    std::optional<std::uint64_t> last_,input_;
public:
    void reset() { pending_=0; last_.reset(); input_.reset(); }
    std::optional<Gesture> update(int delta,std::uint64_t now_ms,unsigned percent=12) {
        if(!delta) return std::nullopt;
        if(input_ && now_ms-*input_>250) pending_=0;
        input_=now_ms;
        if((pending_>0 && delta<0) || (pending_<0 && delta>0)) pending_=0;
        pending_=std::clamp(pending_+std::clamp(delta,-120,120),-120,120);
        if(pending_>-120 && pending_<120) return std::nullopt;
        if(last_ && now_ms-*last_<180) return std::nullopt;
        const bool down=pending_<0; pending_=0; last_=now_ms;
        const unsigned half=65535u*std::clamp(percent,6u,30u)/200u;
        return Gesture{2,{32768,down?32768+half:32768-half},{32768,down?32768-half:32768+half},140};
    }
};
// Reject black bars. Uses the same integer letterbox rectangle as the renderer.
inline std::optional<Point> map_pointer(int x,int y,int vw,int vh,unsigned sw,unsigned sh) {
    if(vw<=0 || vh<=0 || !sw || !sh) return std::nullopt;
    const double scale=std::min(double(vw)/sw,double(vh)/sh);
    const int dw=int(sw*scale),dh=int(sh*scale);
    const int left=(vw-dw)/2,top=(vh-dh)/2,right=(vw+dw)/2,bottom=(vh+dh)/2;
    if(x<left || x>=right || y<top || y>=bottom || right-left<2 || bottom-top<2) return std::nullopt;
    return Point{std::uint32_t(std::uint64_t(x-left)*65535/(right-left-1)),
        std::uint32_t(std::uint64_t(y-top)*65535/(bottom-top-1))};
}
inline Message gesture_message(const Gesture& g,std::uint64_t sequence) {
    if((g.action<1 || g.action>5) || g.start.x>65535 || g.start.y>65535 || g.end.x>65535 || g.end.y>65535 ||
        g.duration_ms<50 || g.duration_ms>500 || (g.action!=2 && (g.start.x!=g.end.x || g.start.y!=g.end.y)))
        throw ProtocolError("invalid gesture");
    Message m; m.header={Type::pointer_action,0,28,2,1,sequence,0};
    for(auto value:{g.action,g.start.x,g.start.y,g.end.x,g.end.y,g.duration_ms,1u})
        for(int shift=24;shift>=0;shift-=8) m.payload.push_back(static_cast<std::uint8_t>(value>>shift));
    return m;
}
}
