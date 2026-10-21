#include "phone_mirror/protocol.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <random>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const pm::ProtocolError&) { rejected = true; }
    check(rejected, "invalid input accepted");
}
}
int main() {
    try {
        std::ifstream input(std::filesystem::path(u8"" PM_FIXTURE_DIR) / "ping.hex");
        std::string hex; input >> hex;
        check(hex.size() == 80, "missing fixture");
        std::vector<std::uint8_t> fixture;
        for (std::size_t i = 0; i < hex.size(); i += 2)
            fixture.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        auto header = pm::decode_header(fixture);
        check(header.type == pm::Type::ping && header.sequence == 1 &&
              header.stream_epoch == 1 && header.timestamp_us == 1000000, "wire fixture mismatch");
        check(pm::encode({header, {}}) == fixture, "wire serialization mismatch");

        pm::Message text;
        text.header.type = pm::Type::text_commit;
        text.payload = {0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd};
        text.header.payload_length = static_cast<std::uint32_t>(text.payload.size());
        auto wire = pm::encode(text);
        wire.insert(wire.end(), fixture.begin(), fixture.end());
        // Every possible split includes payload/header boundaries and an empty message.
        for (std::size_t split = 0; split <= wire.size(); ++split) {
            pm::Parser parser; int received = 0;
            auto consume = [&](pm::Message&& m) {
                if (received++ == 0) check(m.payload == text.payload, "payload corrupt");
                else check(m.header.type == pm::Type::ping, "message order corrupt");
            };
            parser.feed(std::span(wire).first(split), consume);
            parser.feed(std::span(wire).subspan(split), consume);
            parser.finish(); check(received == 2, "message lost across split");
        }
        pm::Parser bytewise; int count = 0;
        for (auto byte : wire) bytewise.feed(std::span(&byte, 1), [&](auto&&) { ++count; });
        bytewise.finish(); check(count == 2, "bytewise parse failed");
        for (std::size_t n = 1; n < fixture.size(); ++n) {
            pm::Parser p; p.feed(std::span(fixture).first(n), [](auto&&) {});
            rejects([&] { p.finish(); });
        }
        auto bad = fixture; bad[0] = 0; rejects([&] { pm::decode_header(bad); });
        bad = fixture; bad[5] = 2; rejects([&] { pm::decode_header(bad); });
        bad = fixture; bad[7] = 255; rejects([&] { pm::decode_header(bad); });
        bad = fixture; bad[8] = 128; rejects([&] { pm::decode_header(bad); });
        bad = fixture; bad[12] = 255;
        pm::Parser poisoned;
        rejects([&] { poisoned.feed(bad, [](auto&&) {}); });
        check(poisoned.failed(), "malformed stream not poisoned");
        rejects([&] { poisoned.feed(fixture, [](auto&&) {}); });
        auto boundary = header; boundary.type = pm::Type::text_commit;
        boundary.payload_length = 16384; pm::encode_header(boundary);
        boundary.payload_length++; rejects([&] { pm::encode_header(boundary); });
        text.header.payload_length++; rejects([&] { pm::encode(text); });
        // Deterministic malformed-header smoke fuzz. No allocation based on unchecked sizes.
        std::mt19937 random(42);
        for (int i = 0; i < 10000; ++i) {
            auto mutated = fixture;
            mutated[random() % mutated.size()] ^= static_cast<std::uint8_t>(random());
            try { pm::decode_header(mutated); } catch (const pm::ProtocolError&) {}
        }
        std::cout << "Protocol contract checks passed (all split points, malformed headers, bounds, truncation, 10000 mutations).\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
