#pragma once

#include "types.hpp"

#include <span>

namespace raealpf {
// GDR binary version 2, 240 Hz. Throws std::runtime_error on invalid or
// unsupported data. Equal-frame events are canonicalized P1 before P2.
// Replay metadata extensions retain the exact frame count despite GDR's
// float32 duration; unknown input extensions and death tracks are unsupported.
std::vector<std::uint8_t> encodeGDR2(Replay const& replay);
// Limits: 64 MiB wire data and decoded input storage, 65535-byte strings.
Replay decodeGDR2(std::span<std::uint8_t const> bytes);
} // namespace raealpf
