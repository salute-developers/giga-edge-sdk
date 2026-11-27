#pragma once

#include <string>

namespace giga_edge::ha {
// A model RGB triplet (R;G;B) to a Home Assistant colour name; invalid input throws BAD_VALUE.
std::string ColourName(const std::string& value);
// A model RGB triplet to the nearest of a few basic Russian colour words, for the model to say; invalid input throws
// BAD_VALUE.
std::string SpokenColour(const std::string& value);
} // namespace giga_edge::ha
