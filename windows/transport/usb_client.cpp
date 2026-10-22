#include <winsock2.h>
#include <ws2tcpip.h>
#include "usb_client.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <deque>

namespace pm {
UsbEndpoint usb_endpoint(unsigned port,std::string_view hex) {
    if (!port || port>65535 || hex.size()!=32) throw ProtocolError("invalid USB endpoint");
    UsbEndpoint result; result.port=static_cast<std::uint16_t>(port);
    const auto digit=[](char c)->unsigned {
        if(c>='0' && c<='9') return c-'0';
        if(c>='a' && c<='f') return c-'a'+10;
        if(c>='A' && c<='F') return c-'A'+10;
        throw ProtocolError("invalid USB credential");
    };
    for(unsigned i=0;i<16;++i) result.token[i]=static_cast<std::uint8_t>(digit(hex[2*i])*16+digit(hex[2*i+1]));
    return result;
}
struct UsbClient::Impl {
    SOCKET socket=INVALID_SOCKET;
    bool wsa=false,ended=false;
    std::stop_token stop;
    std::uint64_t sequence=0;
    using Clock=std::chrono::steady_clock;
    struct Command { std::vector<std::uint8_t> bytes; std::size_t offset=0; Clock::time_point created; };
    std::mutex command_mutex;
    std::deque<Command> commands;
    std::uint64_t command_sequence=0;
    explicit Impl(std::stop_token token):stop(token) {
        WSADATA data{};
        if(WSAStartup(MAKEWORD(2,2),&data)) throw ProtocolError("Winsock startup failed");
        wsa=true;
    }
    ~Impl() {
        if(socket!=INVALID_SOCKET) {
            const char command=4; send(socket,&command,1,0);
            shutdown(socket,SD_BOTH); closesocket(socket);
        }
        if(wsa) WSACleanup();
    }
    bool ready(bool write,Clock::time_point deadline,const char* stage) {
        while(!stop.stop_requested()) {
            if(!write) pump_commands();
            if(Clock::now()>=deadline) throw ProtocolError(stage);
            fd_set set,errors; FD_ZERO(&set); FD_ZERO(&errors); FD_SET(socket,&set); FD_SET(socket,&errors);
            timeval timeout{0,20000};
            const int result=select(0,write?nullptr:&set,write?&set:nullptr,&errors,&timeout);
            if(result==SOCKET_ERROR || FD_ISSET(socket,&errors)) throw ProtocolError("USB socket failed");
            if(result>0) return true;
        }
        return false;
    }
    void pump_commands() {
        std::lock_guard lock(command_mutex);
        while(!commands.empty()) {
            auto& c=commands.front();
            if(Clock::now()-c.created>std::chrono::milliseconds(500)) throw ProtocolError("USB control queue timeout");
            const int count=send(socket,reinterpret_cast<const char*>(c.bytes.data()+c.offset),static_cast<int>(c.bytes.size()-c.offset),0);
            if(count==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK) return;
            if(count<=0) throw ProtocolError("USB control send failed");
            c.offset+=count; if(c.offset==c.bytes.size()) commands.pop_front();
        }
    }
    bool transfer(std::span<std::uint8_t> bytes,bool write,Clock::time_point deadline,const char* stage) {
        std::size_t offset=0;
        while(offset<bytes.size()) {
            if(!ready(write,deadline,stage)) return false;
            auto* p=reinterpret_cast<char*>(bytes.data()+offset);
            const int length=static_cast<int>(std::min<std::size_t>(bytes.size()-offset,65536));
            const int count=write?send(socket,p,length,0):recv(socket,p,length,0);
            if(count==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK) continue;
            if(count<=0) throw ProtocolError("USB disconnected without STOP");
            offset+=count;
        }
        return true;
    }
};
UsbClient::UsbClient(UsbEndpoint endpoint,std::stop_token stop):impl_(std::make_unique<Impl>(stop)) {
    auto& p=*impl_;
    p.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(p.socket==INVALID_SOCKET) throw ProtocolError("USB socket creation failed");
    u_long nonblocking=1;
    if(ioctlsocket(p.socket,FIONBIO,&nonblocking)) throw ProtocolError("USB nonblocking setup failed");
    int yes=1,buffer=64*1024;
    setsockopt(p.socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));
    setsockopt(p.socket,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&buffer),sizeof(buffer));
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(endpoint.port);
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(p.socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR && WSAGetLastError()!=WSAEWOULDBLOCK)
        throw ProtocolError("USB forward connection failed");
    const auto deadline=Impl::Clock::now()+std::chrono::seconds(5);
    if(!p.ready(true,deadline,"USB connect timeout")) return;
    int error=0,length=sizeof(error);
    if(getsockopt(p.socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length) || error)
        throw ProtocolError("USB forward connection failed");
    std::array<std::uint8_t,24> hello{};
    std::memcpy(hello.data(),"PMUSB001",8); std::copy(endpoint.token.begin(),endpoint.token.end(),hello.begin()+8);
    const auto auth_deadline=Impl::Clock::now()+std::chrono::seconds(10);
    const bool sent=p.transfer(hello,true,auth_deadline,"USB authentication send timeout");
    SecureZeroMemory(hello.data(),hello.size()); SecureZeroMemory(endpoint.token.data(),endpoint.token.size());
    if(!sent) return;
    std::array<std::uint8_t,8> ack{};
    if(!p.transfer(ack,false,auth_deadline,"USB authentication response timeout")) return;
    if(std::memcmp(ack.data(),"PMOK0001",8)) throw ProtocolError("USB authentication response rejected");
}
UsbClient::~UsbClient()=default;
bool UsbClient::send_gesture(const Gesture& gesture) {
    auto& p=*impl_; std::lock_guard lock(p.command_mutex);
    if(p.stop.stop_requested() || p.commands.size()>=4) return false;
    p.commands.push_back({encode(gesture_message(gesture,p.command_sequence)),0,Impl::Clock::now()});
    ++p.command_sequence; return true;
}
std::optional<Message> UsbClient::next() {
    auto& p=*impl_;
    if(p.ended || p.stop.stop_requested()) return std::nullopt;
    // Projection/encoder startup can outlast a steady-state frame deadline.
    const bool startup=p.sequence==0;
    const auto deadline=Impl::Clock::now()+std::chrono::seconds(startup?30:10);
    std::array<std::uint8_t,header_size> bytes{};
    if(!p.transfer(bytes,false,deadline,startup?"USB first video packet timeout":"USB video header timeout")) return std::nullopt;
    Message message; message.header=decode_header(bytes);
    const auto& h=message.header;
    if(h.stream_id!=1 || h.stream_epoch!=1 || h.sequence!=p.sequence++) throw ProtocolError("USB stream ordering mismatch");
    if(h.type==Type::stop) {
        if(h.payload_length || h.timestamp_us) throw ProtocolError("invalid USB STOP");
        p.ended=true; return std::nullopt;
    }
    if(h.type!=Type::video_config && h.type!=Type::video_access_unit) throw ProtocolError("unexpected USB message type");
    message.payload.resize(h.payload_length);
    if(!p.transfer(message.payload,false,deadline,"USB video payload timeout")) return std::nullopt;
    return message;
}
}
