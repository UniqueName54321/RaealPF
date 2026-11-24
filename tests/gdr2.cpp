#include "pathfinder/gdr2.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace raealpf;
namespace {
void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action) {
    try { action(); }
    catch (std::runtime_error const&) { return; }
    throw std::runtime_error("Expected malformed/unsupported replay rejection");
}
void same(Replay const& actual, Replay const& expected) {
    require(actual.levelID == expected.levelID, "level ID");
    require(actual.levelName == expected.levelName, "level name");
    require(actual.seed == expected.seed, "seed");
    require(actual.frames == expected.frames, "exact frame count");
    require(actual.platformer == expected.platformer, "platformer");
    require(actual.inputs == expected.inputs, "inputs");
}
// Independently hand-assembled from upstream's actual wire fields, not our
// encoder. duration=1.0f BE, framerate=240.0 double BE, gameVersion=22081.
// P1 events: frame 0 jump down (3), frame 240 left up (1924 => 84 0f).
// P2 events: frame 0 right down (7), frame 1 right up (14).
std::vector<std::uint8_t> fixture() {
    return {
        'G', 'D', 'R', 2, 0, 0, 0,
        0x3f, 0x80, 0, 0, 0xc1, 0xac, 1,
        0x40, 0x6e, 0, 0, 0, 0, 0, 0,
        0xac, 2, 0, 0, 1,
        5, 'O', 't', 'h', 'e', 'r', 1,
        1, 1, 'L', 0, 0, 4, 2,
        3, 0x84, 0x0f, 7, 14
    };
}
void knownBytes() {
    Replay expected;
    expected.levelID = 1;
    expected.levelName = "L";
    expected.seed = 300;
    expected.frames = 240;
    expected.platformer = true;
    expected.inputs = {{0, 1, false, true}, {0, 3, true, true},
        {1, 3, true, false}, {240, 2, false, false}};
    same(decodeGDR2(fixture()), expected);

    // Generic GDR2 tools retain bot identity but strip unknown metadata when
    // re-exporting. Fall back to the standard duration instead of rejecting it.
    auto resaved = fixture();
    resaved.erase(resaved.begin() + 27, resaved.begin() + 34);
    resaved.insert(resaved.begin() + 27, {7, 'R', 'a', 'e', 'a', 'l', 'P', 'F', 1});
    same(decodeGDR2(resaved), expected);

    // Independently specify the exact standard export bytes. Upstream skips
    // the three-byte replay extension and reads the ordinary packed events.
    auto expectedBytes = fixture();
    expectedBytes.erase(expectedBytes.begin() + 27, expectedBytes.begin() + 34);
    expectedBytes.insert(expectedBytes.begin() + 27, {7, 'R', 'a', 'e', 'a', 'l', 'P', 'F', 1});
    // Metadata extension follows level ID and length-prefixed "L".
    expectedBytes.erase(expectedBytes.begin() + 39);
    expectedBytes.insert(expectedBytes.begin() + 39, {3, 1, 0xf0, 1});
    require(encodeGDR2(expected) == expectedBytes, "exact independent export bytes");

    expected.seed = 0xffffffffu;
    expectedBytes.erase(expectedBytes.begin() + 22, expectedBytes.begin() + 24);
    expectedBytes.insert(expectedBytes.begin() + 22, {0xff, 0xff, 0xff, 0xff, 0x0f});
    require(encodeGDR2(expected) == expectedBytes, "32-bit seed fits upstream five-byte reader");
    same(decodeGDR2(expectedBytes), expected);
}
void roundtrips() {
    Replay empty;
    same(decodeGDR2(encodeGDR2(empty)), empty);
    Replay replay;
    replay.levelID = std::numeric_limits<std::uint32_t>::max();
    replay.levelName = std::string("A\0B", 3); // strings are NOT null-terminated
    replay.seed = std::numeric_limits<std::uint32_t>::max();
    replay.frames = 123456789012345ULL; // float duration alone loses frames
    replay.platformer = true;
    replay.inputs = {{0, 1, false, true}, {0, 1, false, false},
        {0, 2, true, true}, {17, 3, false, true}, {65535, 2, true, false},
        {replay.frames, 3, false, false}};
    auto encoded = encodeGDR2(replay);
    same(decodeGDR2(encoded), replay);
    require(encodeGDR2(decodeGDR2(encoded)) == encoded, "deterministic encoding");
    replay.frames = std::numeric_limits<std::uint64_t>::max();
    replay.inputs.clear();
    same(decodeGDR2(encodeGDR2(replay)), replay);
    replay.frames = 1000;
    replay.platformer = false;
    replay.inputs = {{0, 1, true, true}, {63, 1, true, false},
        {64, 1, false, true}, {1000, 1, false, false}};
    same(decodeGDR2(encodeGDR2(replay)), replay);
    replay.inputs = {{0, 1, true, true}, {0, 1, false, true}};
    auto canonical = decodeGDR2(encodeGDR2(replay));
    require(!canonical.inputs[0].player2 && canonical.inputs[1].player2, "P1 tie ordering");
}
void malformed() {
    auto good = fixture();
    for (std::size_t size = 0; size < good.size(); ++size)
        rejects([&] { decodeGDR2(std::span(good).first(size)); });
    auto bad = good;
    bad.push_back(0);
    rejects([&] { decodeGDR2(bad); });
    bad = good; bad[0] = 'X';
    rejects([&] { decodeGDR2(bad); });
    bad = good; bad[3] = 3;
    rejects([&] { decodeGDR2(bad); });
    bad = good; bad[3] = 0x82; bad.insert(bad.begin() + 4, 0);
    rejects([&] { decodeGDR2(bad); }); // nonminimal varint
    bad = {'G', 'D', 'R'}; bad.insert(bad.end(), 10, 0x80);
    rejects([&] { decodeGDR2(bad); });
    bad.back() = 2;
    rejects([&] { decodeGDR2(bad); }); // uint64 varint overflow
    bad = good; bad[4] = 0x80; bad[5] = 0x80; bad[6] = 4;
    rejects([&] { decodeGDR2(bad); }); // string > 65535
    bad = good; bad[4] = 1; bad.insert(bad.begin() + 5, 'X');
    rejects([&] { decodeGDR2(bad); }); // custom input semantics
    bad = good; bad[7] = 0x7f; bad[8] = 0x80;
    rejects([&] { decodeGDR2(bad); }); // infinite duration
    bad = good; bad[7] = 0xbf;
    rejects([&] { decodeGDR2(bad); }); // negative duration
    bad = good; bad[15] = 0x7e;
    rejects([&] { decodeGDR2(bad); }); // unsupported tickrate
    bad = good; bad[26] = 2;
    rejects([&] { decodeGDR2(bad); }); // boolean range
    bad = good; bad[37] = 127;
    rejects([&] { decodeGDR2(bad); }); // extension bounds
    bad = good; bad[38] = 1;
    rejects([&] { decodeGDR2(bad); }); // death track
    bad = good; bad[40] = 5;
    rejects([&] { decodeGDR2(bad); }); // P1 > total
    bad = good; bad[41] = 1;
    rejects([&] { decodeGDR2(bad); }); // platformer button 0
    bad = good; bad.back() = 0x7e;
    // delta 15 instead of 1 remains valid; force second P2 event past duration.
    bad.pop_back(); bad.insert(bad.end(), {0xfe, 0x7f});
    rejects([&] { decodeGDR2(bad); });

    Replay replay;
    replay.frames = 3;
    replay.inputs = {{2, 1, false, true}, {1, 1, false, false}};
    rejects([&] { encodeGDR2(replay); });
    replay.inputs = {{4, 1, false, true}};
    rejects([&] { encodeGDR2(replay); });
    replay.inputs = {{0, 2, false, true}};
    rejects([&] { encodeGDR2(replay); }); // left in non-platformer
    replay.platformer = true;
    replay.inputs[0].button = 0;
    rejects([&] { encodeGDR2(replay); });
    replay.inputs.clear(); replay.levelName.assign(65536, 'x');
    rejects([&] { encodeGDR2(replay); });
    replay.levelName.clear();
    replay.frames = std::numeric_limits<std::uint64_t>::max();
    replay.inputs = {{replay.frames, 1, false, true}};
    rejects([&] { encodeGDR2(replay); }); // packed delta overflow
    auto own = encodeGDR2(Replay{});
    own.push_back(0);
    rejects([&] { decodeGDR2(own); });
    Replay huge;
    huge.frames = std::numeric_limits<std::uint64_t>::max();
    huge.platformer = true;
    auto overflow = encodeGDR2(huge);
    overflow.resize(overflow.size() - 2); // replace input count and P1 count
    overflow.insert(overflow.end(), {9, 9});
    for (int event = 0; event < 9; ++event) {
        overflow.insert(overflow.end(), 9, 0xff);
        overflow.push_back(1); // UINT64_MAX: delta=(2^61)-1, right down
    }
    rejects([&] { decodeGDR2(overflow); }); // cumulative frame overflow
}
} // namespace

int main() {
    try {
        knownBytes();
        roundtrips();
        malformed();
        std::cout << "GDR2 codec regressions passed\n";
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
