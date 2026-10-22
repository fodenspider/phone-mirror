#include <winsock2.h>
#include "usb_client.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void write_all(SOCKET peer,std::span<const std::uint8_t> bytes) {
    for(const auto byte:bytes) { const char c=static_cast<char>(byte); if(send(peer,&c,1,0)!=1) return; }
}
struct Server {
    SOCKET listener=INVALID_SOCKET;
    unsigned port=0;
    std::jthread thread;
    std::atomic<bool> hello_ok{false};
    explicit Server(std::function<void(SOCKET)> action) {
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        check(listener!=INVALID_SOCKET,"listen socket");
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"bind");
        int length=sizeof(address); getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
        port=ntohs(address.sin_port); check(listen(listener,1)==0,"listen");
        thread=std::jthread([this,action] {
            SOCKET peer=accept(listener,nullptr,nullptr); if(peer==INVALID_SOCKET) return;
            DWORD timeout=2000; setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
            std::array<char,24> hello{}; int used=0;
            while(used<24) { const int n=recv(peer,hello.data()+used,24-used,0); if(n<=0) break; used+=n; }
            hello_ok=used==24 && std::memcmp(hello.data(),"PMUSB001",8)==0;
            for(unsigned i=8;i<24;++i) if(hello[i]!=0x11) hello_ok=false;
            action(peer); shutdown(peer,SD_BOTH); closesocket(peer);
        });
    }
    ~Server() { closesocket(listener); if(thread.joinable()) thread.join(); }
    pm::UsbEndpoint endpoint() const { return pm::usb_endpoint(port,std::string(32,'1')); }
};
void ack(SOCKET peer) { send(peer,"PMOK0001",8,0); }
template<class F> void rejected(F action) {
    bool failed=false; try { action(); } catch(const pm::ProtocolError&) { failed=true; }
    check(failed,"malformed peer must be rejected");
}
}
int main() {
    WSADATA wsa{}; if(WSAStartup(MAKEWORD(2,2),&wsa)) return 1;
    try {
        rejected([] { pm::usb_endpoint(0,std::string(32,'1')); });
        rejected([] { pm::usb_endpoint(1,std::string(32,'z')); });
        {
            Server server([](SOCKET peer) {
                ack(peer); pm::Message m; m.header={pm::Type::video_config,0,3,1,1,0,0}; m.payload={1,2,3};
                write_all(peer,pm::encode(m)); m.header={pm::Type::stop,0,0,1,1,1,0}; m.payload.clear(); write_all(peer,pm::encode(m));
            });
            pm::UsbClient client(server.endpoint()); auto message=client.next();
            check(message && message->payload==std::vector<std::uint8_t>({1,2,3}),"fragmented message");
            check(!client.next() && !client.next(),"terminal STOP"); check(server.hello_ok,"credential encoding");
        }
        {
            Server server([](SOCKET peer) { send(peer,"BAD00000",8,0); });
            rejected([&] { pm::UsbClient client(server.endpoint()); });
        }
        {
            Server server([](SOCKET peer) { ack(peer); send(peer,"PM",2,0); });
            rejected([&] { pm::UsbClient client(server.endpoint()); client.next(); });
        }
        for(unsigned mode=0;mode<4;++mode) {
            Server server([mode](SOCKET peer) {
                ack(peer); pm::Header h{pm::Type::video_access_unit,0,4,1,1,0,0};
                if(mode==1) h.sequence=1;
                if(mode==2) { h.type=pm::Type::stop; h.payload_length=1; }
                auto bytes=pm::encode_header(h);
                if(mode==3) { bytes[12]=0x7f; bytes[13]=0xff; bytes[14]=0xff; bytes[15]=0xff; }
                write_all(peer,bytes); // mode 0: missing payload, mode 3: reject before allocation
            });
            rejected([&] { pm::UsbClient client(server.endpoint()); client.next(); });
        }
        {
            Server server([](SOCKET peer) { ack(peer); char command=0; recv(peer,&command,1,0); });
            std::stop_source stop;
            std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); stop.request_stop(); });
            const auto start=std::chrono::steady_clock::now();
            { pm::UsbClient client(server.endpoint(),stop.get_token()); check(!client.next(),"cancel silent peer"); }
            check(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"cancellation deadline");
        }
        std::cout<<"USB transport tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; WSACleanup(); return 1; }
    WSACleanup(); return 0;
}
