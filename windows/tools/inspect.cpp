#include "phone_mirror/protocol.hpp"
#include <array>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: pm-inspect <capture.pmstream>\n"; return 2; }
    try {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("cannot open stream");
        pm::Parser parser; std::array<std::uint8_t, 65536> buffer{};
        std::uint64_t messages = 0, video = 0, bytes = 0;
        while (file) {
            file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
            parser.feed(std::span(buffer).first(static_cast<std::size_t>(file.gcount())), [&](pm::Message&& m) {
                ++messages; bytes += m.payload.size();
                if (m.header.type == pm::Type::video_access_unit) ++video;
            });
        }
        if (!file.eof()) throw std::runtime_error("read error");
        parser.finish();
        std::cout << "messages=" << messages << " video_access_units=" << video << " payload_bytes=" << bytes << '\n';
        if (messages == 0) throw std::runtime_error("empty stream");
        return 0;
    } catch (const std::exception& e) { std::cerr << "Invalid stream: " << e.what() << '\n'; return 1; }
}
