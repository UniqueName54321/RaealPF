#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace raealpf {
inline constexpr unsigned TickRate = 240;
struct Input {
    std::uint64_t frame = 0;
    std::uint8_t button = 1;
    bool player2 = false;
    bool down = false;
    bool operator==(Input const&) const = default;
};
struct Replay {
    std::uint32_t levelID = 0;
    std::string levelName;
    std::uint32_t seed = 0;
    std::uint64_t frames = 0;
    bool platformer = false;
    std::vector<Input> inputs;
};
}
