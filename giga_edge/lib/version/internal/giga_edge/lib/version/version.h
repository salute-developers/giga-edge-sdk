#pragma once

#include <string>
#include <vector>

namespace giga_edge {

constexpr char kLibVersion[] = "1.35";
constexpr char kUnknownVersion[] = "unk_version";

void CheckIfCompatible(const std::string& version);

} // namespace giga_edge
