#pragma once

#include <string_view>

namespace giga_edge {

template<typename Enum>
std::string_view EnumToString(Enum value);

template<typename Enum>
Enum EnumFromString(std::string_view name);

} // namespace giga_edge
