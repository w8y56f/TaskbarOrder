#include "common/accessor.hpp"
#include <array>
#include <iostream>
#include <utility>

int main() {
    const std::array<unsigned char, 7> compact{0x48,0x8b,0x41,0x28,0x8b,0x00,0xc3};
    const std::array<unsigned char, 10> extended{0x48,0x8b,0x81,0x00,0x01,0x00,0x00,0x8b,0x00,0xc3};
    const std::array<unsigned char, 14> guarded{0x48,0x8b,0x51,0x20,0x33,0xc0,0x48,0x85,0xd2,0x74,0x02,0x8b,0x02,0xc3};
    if (tbo::task_items_offset(compact) != 40 || tbo::task_items_offset(extended) != 256 ||
        tbo::task_items_offset(guarded) != 32) return 1;
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
    for (std::size_t length = 0; length < guarded.size(); ++length)
        if (tbo::task_items_offset(std::span(guarded).first(length))) return 6;
    for (auto [index, replacement] : {std::pair{2u, 0x41u}, std::pair{5u, 0xc1u},
                                      std::pair{8u, 0xc9u}, std::pair{9u, 0x75u},
                                      std::pair{10u, 0x03u}, std::pair{12u, 0x00u}}) {
        auto changed = guarded; changed[index] = static_cast<unsigned char>(replacement);
        if (tbo::task_items_offset(changed)) return 7;
    }
    for (unsigned char offset : {0, 1, 7, 9, 127, 128, 248, 255}) {
        auto changed = guarded; changed[3] = offset;
        if (tbo::task_items_offset(changed)) return 8;
    }
    for (auto zeroing : {0x31, 0x2b, 0x29}) {
        auto changed = guarded; changed[4] = static_cast<unsigned char>(zeroing);
        if (tbo::task_items_offset(changed) != 32) return 9;
    }
    std::cout << "Known accessor layouts accepted; truncation, changed opcodes and invalid offsets rejected.\n";
}
