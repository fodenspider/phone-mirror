#include "playback.hpp"
#include "usb_client.hpp"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=5 || std::wstring(argv[1])!=L"--port" || std::wstring(argv[3])!=L"--seconds")
            throw std::runtime_error("usage: pm-live-check --port PORT --seconds 1..120 (credential via stdin)");
        std::size_t used=0; const auto port=std::stoul(argv[2],&used);
        if(used!=std::wstring(argv[2]).size() || port>65535) throw std::runtime_error("invalid port");
        const auto seconds=std::stoul(argv[4],&used);
        if(used!=std::wstring(argv[4]).size() || !seconds || seconds>120) throw std::runtime_error("invalid duration");
        std::string secret(32,'\0'); std::cin.read(secret.data(),32);
        if(std::cin.gcount()!=32) throw std::runtime_error("USB credential missing");
        auto endpoint=pm::usb_endpoint(static_cast<unsigned>(port),secret);
        SecureZeroMemory(secret.data(),secret.size());
        std::stop_source stop;
        std::jthread timer([&](std::stop_token token) {
            std::mutex mutex; std::unique_lock lock(mutex); std::condition_variable_any wake;
            wake.wait_for(lock,token,std::chrono::seconds(seconds),[]{ return false; });
            if(!token.stop_requested()) stop.request_stop();
        });
        const auto start=std::chrono::steady_clock::now();
        pm::UsbClient client(endpoint,stop.get_token());
        pm::PlaybackOptions options;
        const auto stats=pm::play_stream([&] { return client.next(); },options,stop.get_token());
        timer.request_stop();
        if(!stats.decoded_frames) throw std::runtime_error("live decoder produced no frames");
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"{\"input_frames\":"<<stats.input_frames<<",\"decoded_frames\":"<<stats.decoded_frames
            <<",\"gpu_frames\":"<<stats.gpu_frames<<",\"cpu_frames\":"<<stats.cpu_frames
            <<",\"width\":"<<stats.width<<",\"height\":"<<stats.height<<",\"elapsed_ms\":"<<elapsed<<"}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
