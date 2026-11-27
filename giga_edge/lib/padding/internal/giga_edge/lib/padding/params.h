#pragma once

namespace giga_edge {

enum class PaddingAppendPlace { BEGIN, END };

enum class PaddingStrategy { ALWAYS_ADD, PAD_TO_SIZE };

struct PaddingParams {
  int count;
  float value;
  PaddingAppendPlace append_place = PaddingAppendPlace::BEGIN;
  PaddingStrategy padding_strategy = PaddingStrategy::ALWAYS_ADD;
};

} // namespace giga_edge