// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

namespace tbo {
// The destination index is in the final array, after removal of the source.
inline std::optional<std::size_t> insertion_index(std::size_t count,
                                                std::size_t source,
                                                std::size_t target,
                                                bool after) {
    if (source >= count || target >= count) return std::nullopt;
    if (source == target) return source;
    auto destination = target + (after ? 1 : 0);
    if (source < destination) --destination;
    return destination;
}

template<class T> bool move_to(std::span<T> items, std::size_t from, std::size_t to) {
    if (from >= items.size() || to >= items.size()) return false;
    if (from < to) std::rotate(items.begin() + from, items.begin() + from + 1,
                               items.begin() + to + 1);
    if (from > to) std::rotate(items.begin() + to, items.begin() + from,
                               items.begin() + from + 1);
    return true;
}
}
