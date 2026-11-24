#include "gdr2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace raealpf {
namespace {
// Wire reference: maxnut/GDReplayFormat gdr2, revision
// d8d2f0b1193a46cdf47b413dc832616dd052b6a9, gdr.hpp + binarystream.hpp.
// This implementation is original. The README's cstring/integer-float tables
// differ from that revision's actual codec: strings have LEB128 lengths and
// duration/framerate are big-endian IEEE754 float32/float64 respectively.
constexpr std::size_t MaxBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxString = 65535; // upstream reader's string limit
constexpr std::string_view Bot = "RaealPF";
constexpr std::uint64_t GameVersion = 22081;
static_assert(sizeof(float) == 4 && sizeof(double) == 8);
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);

[[noreturn]] void fail(char const* text) {
    throw std::runtime_error(text);
}
std::size_t varSize(std::uint64_t value) {
    std::size_t size = 1;
    while (value >= 128) { value >>= 7; ++size; }
    return size;
}
std::size_t stringSize(std::string_view value) {
    if (value.size() > MaxString) fail("GDR2 string too long");
    return varSize(value.size()) + value.size();
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void var(std::uint64_t value) {
        do {
            auto byte = static_cast<std::uint8_t>(value & 127);
            value >>= 7;
            bytes.push_back(static_cast<std::uint8_t>(byte | (value ? 128 : 0)));
        } while (value);
    }
    void string(std::string_view value) {
        var(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    template<class U> void big(U value) {
        for (std::size_t i = sizeof(U); i != 0; --i)
            bytes.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
    }
};
struct Reader {
    std::span<std::uint8_t const> bytes;
    std::uint8_t byte() {
        if (bytes.empty()) fail("Truncated GDR2 data");
        auto result = bytes.front();
        bytes = bytes.subspan(1);
        return result;
    }
    std::uint64_t var() {
        std::uint64_t result = 0;
        for (unsigned i = 0; i < 10; ++i) {
            auto b = byte();
            if (i == 9 && b > 1) fail("GDR2 varint overflow");
            result |= std::uint64_t(b & 127) << (7 * i);
            if (!(b & 128)) {
                if (i && b == 0) fail("Noncanonical GDR2 varint");
                return result;
            }
        }
        fail("Unterminated GDR2 varint");
    }
    std::span<std::uint8_t const> take(std::uint64_t count) {
        if (count > bytes.size()) fail("Truncated GDR2 block");
        auto result = bytes.first(static_cast<std::size_t>(count));
        bytes = bytes.subspan(static_cast<std::size_t>(count));
        return result;
    }
    std::string_view string() {
        auto count = var();
        if (count > MaxString) fail("GDR2 string too long");
        auto value = take(count);
        return {reinterpret_cast<char const*>(value.data()), value.size()};
    }
    bool boolean() {
        auto value = var();
        if (value > 1) fail("Invalid GDR2 boolean");
        return value != 0;
    }
    template<class U> U big() {
        U result = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i)
            result = (result << 8) | byte();
        return result;
    }
    std::uint32_t u32() {
        auto value = var();
        if (value > std::numeric_limits<std::uint32_t>::max())
            fail("GDR2 integer out of range");
        return static_cast<std::uint32_t>(value);
    }
};
float durationFor(std::uint64_t frames) {
    return static_cast<float>(static_cast<long double>(frames) / TickRate);
}
} // namespace

std::vector<std::uint8_t> encodeGDR2(Replay const& replay) {
    // Bound resources before any output allocation, including empty events.
    if (replay.inputs.size() > MaxBytes / sizeof(Input)) fail("GDR2 has too many inputs");
    std::size_t p1Count = 0;
    std::uint64_t previous = 0;
    for (auto const& input : replay.inputs) {
        if (input.frame < previous || input.frame > replay.frames)
            fail("GDR2 inputs must be ordered and within duration");
        if (input.button < 1 || input.button > 3 || (!replay.platformer && input.button != 1))
            fail("Invalid GDR2 button");
        previous = input.frame;
        p1Count += !input.player2;
    }
    auto extensionSize = 1 + varSize(replay.frames); // version byte + exact frames
    std::size_t size = 3 + 1 + 1 + 1 + 1 + 4 + varSize(GameVersion) + 8
        + varSize(replay.seed) + 1 + 1 + 1 + stringSize(Bot) + 1
        + varSize(replay.levelID) + stringSize(replay.levelName)
        + varSize(extensionSize) + extensionSize + 1
        + varSize(replay.inputs.size()) + varSize(p1Count);
    unsigned shift = replay.platformer ? 3 : 1;
    std::uint64_t last[2] = {};
    for (auto const& input : replay.inputs) {
        auto delta = input.frame - last[input.player2];
        if (delta > (std::numeric_limits<std::uint64_t>::max() >> shift))
            fail("GDR2 input delta overflows packed event");
        auto packed = (delta << shift) | (replay.platformer ? input.button << 1 : 0) | input.down;
        size += varSize(packed);
        if (size > MaxBytes) fail("GDR2 file too large");
        last[input.player2] = input.frame;
    }
    if (size > MaxBytes) fail("GDR2 file too large");
    Writer out;
    out.bytes.reserve(size);
    out.bytes.insert(out.bytes.end(), {'G', 'D', 'R'});
    out.var(2);
    out.string(""); // no input extensions
    out.string(""); // author
    out.string(""); // description
    out.big(std::bit_cast<std::uint32_t>(durationFor(replay.frames)));
    out.var(GameVersion);
    out.big(std::bit_cast<std::uint64_t>(static_cast<double>(TickRate)));
    // Encode the 32-bit seed bits without sign extension: upstream's int reader
    // consumes at most five bytes, even though its negative-int writer emits ten.
    out.var(replay.seed);
    out.var(0); // coins
    out.var(0); // LDM
    out.var(replay.platformer);
    out.string(Bot);
    out.var(1);
    out.var(replay.levelID);
    out.string(replay.levelName);
    out.var(extensionSize);
    out.var(1); // RaealPF metadata extension version
    out.var(replay.frames);
    out.var(0); // death count: only complete, uninterrupted attempts
    out.var(replay.inputs.size());
    out.var(p1Count);
    for (bool player2 : {false, true}) {
        previous = 0;
        for (auto const& input : replay.inputs) {
            if (input.player2 != player2) continue;
            auto delta = input.frame - previous;
            out.var((delta << shift) | (replay.platformer ? input.button << 1 : 0) | input.down);
            previous = input.frame;
        }
    }
    return std::move(out.bytes);
}

Replay decodeGDR2(std::span<std::uint8_t const> bytes) {
    if (bytes.size() > MaxBytes) fail("GDR2 file too large");
    Reader in{bytes};
    if (in.byte() != 'G' || in.byte() != 'D' || in.byte() != 'R') fail("Invalid GDR2 magic");
    if (in.var() != 2) fail("Unsupported GDR binary version");
    if (!in.string().empty()) fail("Unsupported GDR2 input extensions");
    in.string(); // author
    in.string(); // description
    float duration = std::bit_cast<float>(in.big<std::uint32_t>());
    if (!std::isfinite(duration) || duration < 0) fail("Invalid GDR2 duration");
    in.u32(); // game version
    double rate = std::bit_cast<double>(in.big<std::uint64_t>());
    if (rate != TickRate) fail("Unsupported GDR2 tickrate (requires 240 Hz)");
    Replay replay;
    auto seed = in.var();
    if (seed > std::numeric_limits<std::uint32_t>::max() && seed < 0xffffffff80000000ULL)
        fail("GDR2 seed out of range");
    replay.seed = static_cast<std::uint32_t>(seed);
    in.u32(); // coins
    in.boolean(); // LDM
    replay.platformer = in.boolean();
    auto bot = in.string();
    auto botVersion = in.u32();
    replay.levelID = in.u32();
    replay.levelName = in.string();
    Reader extension{in.take(in.var())};
    if (bot == Bot && botVersion == 1 && !extension.bytes.empty()) {
        if (extension.var() != 1) fail("Unsupported RaealPF metadata version");
        replay.frames = extension.var();
        if (!extension.bytes.empty()) fail("Trailing RaealPF metadata");
        if (durationFor(replay.frames) != duration) fail("GDR2 duration disagrees with exact frames");
    } else {
        // Generic metadata cannot represent exact frames above float precision.
        long double ticks = static_cast<long double>(duration) * TickRate;
        long double rounded = std::floor(ticks + 0.5L);
        if (rounded >= std::ldexp(1.0L, 64)) fail("GDR2 duration overflows frames");
        replay.frames = static_cast<std::uint64_t>(rounded);
        if (!extension.bytes.empty()) fail("Unsupported GDR2 replay extension");
    }
    if (in.var() != 0) fail("Unsupported GDR2 death track");
    auto count = in.var();
    auto p1Count = in.var();
    if (p1Count > count || count > in.bytes.size() || count > MaxBytes / sizeof(Input))
        fail("Invalid GDR2 input counts");
    replay.inputs.reserve(static_cast<std::size_t>(count));
    std::uint64_t frame = 0;
    unsigned shift = replay.platformer ? 3 : 1;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (i == p1Count) frame = 0;
        auto packed = in.var();
        auto delta = packed >> shift;
        if (delta > std::numeric_limits<std::uint64_t>::max() - frame)
            fail("GDR2 frame addition overflow");
        frame += delta;
        auto button = static_cast<std::uint8_t>(replay.platformer ? (packed >> 1) & 3 : 1);
        if (button == 0 || frame > replay.frames) fail("GDR2 input outside replay range");
        replay.inputs.push_back({frame, button, i >= p1Count, bool(packed & 1)});
    }
    if (!in.bytes.empty()) fail("Trailing GDR2 bytes");
    // Each player's stream is ordered already; merge without changing that
    // player's same-frame press/release order. Upstream uses the same P1 tie.
    std::inplace_merge(replay.inputs.begin(), replay.inputs.begin() + static_cast<std::ptrdiff_t>(p1Count),
        replay.inputs.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    return replay;
}
} // namespace raealpf
