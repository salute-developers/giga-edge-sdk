#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace giga_edge {

struct StripLeadingWordsNodeParams {
  std::vector<std::string> words;
  char words_delimiter = ',';
  bool repeat = true;
  int typos_per_letters = 0;
  int max_skipped_tokens = 0;
  bool split_glued = false;
  bool capitalize_rest = false;
  bool drop_unmatched = false;
  bool hold_words = false;
};

} // namespace giga_edge
