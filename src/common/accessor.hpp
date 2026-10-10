// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tbo {
// Strict x64 whitelist for the observed GetNumItems implementations.
// Any changed instruction sequence is unsupported, rather than guessed.
inline std::optional<std::size_t> task_items_offset(std::span<const unsigned char> code) {
    std::uint32_t offset{};
    if (code.size() >= 7 && code[0] == 0x48 && code[1] == 0x8b && code[2] == 0x41 &&
        code[4] == 0x8b && code[5] == 0x00 && code[6] == 0xc3) {
        if (code[3] >= 0x80) return {};
        offset = code[3];
    } else if (code.size() >= 10 && code[0] == 0x48 && code[1] == 0x8b && code[2] == 0x81 &&
               code[7] == 0x8b && code[8] == 0x00 && code[9] == 0xc3) {
        for (unsigned i = 0; i < 4; ++i) offset |= static_cast<std::uint32_t>(code[3 + i]) << (8 * i);
    } else if (code.size() >= 14 && code[0] == 0x48 && code[1] == 0x8b && code[2] == 0x51 &&
               // mov rdx,[rcx+disp8]; zero eax; test rdx,rdx; je return;
               // mov eax,[rdx]; ret. The null path returns zero.
               ((code[4] == 0x33 && code[5] == 0xc0) ||
                (code[4] == 0x31 && code[5] == 0xc0) ||
                (code[4] == 0x2b && code[5] == 0xc0) ||
                (code[4] == 0x29 && code[5] == 0xc0)) &&
               code[6] == 0x48 && code[7] == 0x85 && code[8] == 0xd2 &&
               code[9] == 0x74 && code[10] == 0x02 &&
               code[11] == 0x8b && code[12] == 0x02 && code[13] == 0xc3) {
        if (code[3] >= 0x80) return {};
        offset = code[3];
    } else return {};
    if (offset < 8 || offset > 1024 || offset % 8) return {};
    return offset;
}
}
