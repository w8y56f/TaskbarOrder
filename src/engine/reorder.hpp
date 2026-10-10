// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/protocol.hpp"
#include <string>

namespace tbo::engine {
bool start(const Plan& plan, DWORD controllerPid, std::wstring& message);
void stop() noexcept;
}
