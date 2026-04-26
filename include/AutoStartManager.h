#pragma once

#include <string>

namespace AutoStart {

bool IsEnabled();
bool SetEnabled(bool enabled);
std::wstring CommandLine();

} // namespace AutoStart
