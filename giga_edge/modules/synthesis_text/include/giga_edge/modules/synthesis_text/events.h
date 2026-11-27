#pragma once

#include <string>

namespace giga_edge {

struct SetSynthesisTextEvent {
  std::string synthesis_text;
  bool force;
};

struct ResetSynthesisTextEvent {
  bool force;
};

} // namespace giga_edge