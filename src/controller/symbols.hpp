// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/windows.hpp"
#include <functional>

namespace tbo {
using Progress = std::function<void(std::wstring)>;
Plan prepare_symbols(const Environment& environment, bool allowDownload, const Progress& progress,
                     const std::function<bool()>& canceled = [] { return false; });
void save_plan(const Environment& environment, const Plan& plan);
Plan load_plan(const Environment& environment);
}
