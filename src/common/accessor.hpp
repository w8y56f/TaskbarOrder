// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tbo {
// Strict x64 whitelist: mov rax,[rcx+disp]; mov eax,[rax]; ret.
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
    } else return {};
    if (offset < 8 || offset > 1024 || offset % 8) return {};
    return offset;
}
}
