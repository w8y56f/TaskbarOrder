#include "common/order.hpp"
#include <array>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    // Independent remove/insert oracle covers both directions, adjacency,
    // self-drop, and the distinction between dropping before and after.
    for (std::size_t n = 1; n <= 32; ++n) {
        for (std::size_t from = 0; from < n; ++from) {
            for (std::size_t target = 0; target < n; ++target) {
                for (bool after : {false, true}) {
                    std::vector<int> actual(n), expected;
                    std::iota(actual.begin(), actual.end(), 0);
                    if (from == target) expected = actual;
                    else {
                        for (std::size_t i = 0; i < n; ++i) {
                            if (i == target && !after) expected.push_back(static_cast<int>(from));
                            if (i != from) expected.push_back(static_cast<int>(i));
                            if (i == target && after) expected.push_back(static_cast<int>(from));
                        }
                    }
                    auto dest = tbo::insertion_index(n, from, target, after);
                    if (!dest || !tbo::move_to<int>(actual, from, *dest) || actual != expected) {
                        std::cerr << "Incorrect order: " << n << ' ' << from << ' ' << target << ' ' << after << '\n';
                        return 1;
                    }
                }
            }
        }
    }
    if (tbo::insertion_index(0, 0, 0, false) || tbo::insertion_index(3, 3, 0, true) ||
        tbo::insertion_index(3, 0, 3, false)) return 2;
    std::array<int, 3> a{0, 1, 2};
    if (tbo::move_to<int>(a, 0, 3) || a != std::array<int, 3>{0, 1, 2}) return 3;
    std::cout << "Ordering oracle and invalid-index checks passed.\n";
}
