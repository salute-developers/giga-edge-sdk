#pragma once

#include <string>

namespace giga_edge {

enum class PersonalizationPronoun { FORMAL = 0, INFORMAL };

struct Personalization {
  PersonalizationPronoun pronoun = PersonalizationPronoun::FORMAL;
  // The voice in the host terms (for example, a character); empty if the host has not chosen one.
  std::string voice;
};

} // namespace giga_edge
