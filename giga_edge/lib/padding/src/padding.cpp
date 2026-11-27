#include "giga_edge/lib/padding/padding.h"

namespace giga_edge {

void ApplyPadding(std::vector<float>& vec, const PaddingParams& params) {
  if (params.count <= 0) {
    return;
  }

  int elements_to_add = 0;

  switch (params.padding_strategy) {
    case PaddingStrategy::PAD_TO_SIZE:
      if (vec.size() >= params.count) {
        return;
      }
      elements_to_add = params.count - vec.size();
      break;
    case PaddingStrategy::ALWAYS_ADD:
      elements_to_add = params.count;
      break;
  }

  switch (params.append_place) {
    case PaddingAppendPlace::END:
      vec.insert(vec.end(), elements_to_add, params.value);
      break;
    case PaddingAppendPlace::BEGIN:
      std::vector<float> tmp(elements_to_add, params.value);
      tmp.insert(tmp.end(), vec.begin(), vec.end());
      std::swap(vec, tmp);
      break;
  }
}

} // namespace giga_edge
