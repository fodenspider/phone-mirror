#pragma once
#include "protocol.hpp"
#include <optional>
#include <algorithm>

namespace pm {
struct Point { std::uint32_t x=0,y=0; };
struct Gesture { std::uint32_t action=1; Point start,end; std::uint32_t duration_ms=50; };
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
    if((g.action!=1 && g.action!=2) || g.start.x>65535 || g.start.y>65535 || g.end.x>65535 || g.end.y>65535 ||
        g.duration_ms<50 || g.duration_ms>500 || (g.action==1 && (g.start.x!=g.end.x || g.start.y!=g.end.y)))
        throw ProtocolError("invalid gesture");
    Message m; m.header={Type::pointer_action,0,28,2,1,sequence,0};
    for(auto value:{g.action,g.start.x,g.start.y,g.end.x,g.end.y,g.duration_ms,1u})
        for(int shift=24;shift>=0;shift-=8) m.payload.push_back(static_cast<std::uint8_t>(value>>shift));
    return m;
}
}
