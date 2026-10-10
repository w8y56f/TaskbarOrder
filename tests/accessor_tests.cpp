#include "common/accessor.hpp"
#include <array>
#include <iostream>

int main() {
    const std::array<unsigned char, 7> compact{0x48,0x8b,0x41,0x28,0x8b,0x00,0xc3};
    const std::array<unsigned char, 10> extended{0x48,0x8b,0x81,0x00,0x01,0x00,0x00,0x8b,0x00,0xc3};
    if (tbo::task_items_offset(compact) != 40 || tbo::task_items_offset(extended) != 256) return 1;
    for (std::size_t length = 0; length < compact.size(); ++length)
        if (tbo::task_items_offset(std::span(compact).first(length))) return 2;
    for (std::size_t i = 0; i < compact.size(); ++i) {
        if (i == 3) continue;
        auto changed = compact; changed[i] ^= 1;
        if (tbo::task_items_offset(changed)) return 3;
    }
    for (unsigned char offset : {0, 1, 7, 9, 127, 128, 248, 255}) {
        auto changed = compact; changed[3] = offset;
        if (tbo::task_items_offset(changed)) return 4;
    }
    auto outside = extended; outside[4] = 0x05;
    if (tbo::task_items_offset(outside)) return 5;
    std::cout << "Known accessor layouts accepted; truncation, changed opcodes and invalid offsets rejected.\n";
}
