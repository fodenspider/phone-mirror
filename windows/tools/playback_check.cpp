#include "playback.hpp"
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv) {
    if(argc<2) { std::cerr << "Usage: pm-playback-check <capture.pmstream> [--software] [--snapshot output.bmp]\n"; return 2; }
    try {
        pm::PlaybackOptions options;
        for(int i=2;i<argc;++i) {
            const std::wstring argument=argv[i];
            if(argument==L"--software") options.software=true;
            else if(argument==L"--snapshot" && i+1<argc) options.snapshot=argv[++i];
            else throw std::runtime_error("unknown or incomplete option");
        }
        const auto s=pm::play_capture(argv[1],options);
        std::cout << "input_frames=" << s.input_frames << " decoded_frames=" << s.decoded_frames
            << " gpu_surface_frames=" << s.gpu_frames << " cpu_frames=" << s.cpu_frames
            << " dimensions=" << s.width << 'x' << s.height << " d3d_manager=" << s.d3d_manager << '\n';
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
