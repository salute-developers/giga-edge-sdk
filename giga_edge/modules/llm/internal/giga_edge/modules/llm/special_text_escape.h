#pragma once

#include "giga_edge/modules/assistant_core/prompt_template.h"

#include <string>
#include <string_view>
#include <vector>

namespace giga_edge {

// The prompt is tokenized with special tokens parsed. An occurrence of a special text stays a special token only if
// all its bytes come from one trusted source: entirely the template or entirely one verified piece. Other
// occurrences are broken by inserting U+200B at the source boundary inside the occurrence, and an entirely foreign
// one after its first code point; a special text of one code point is replaced with U+FFFD. Without such occurrences
// the text does not change.
// spans are ascending and do not overlap; special_texts are valid UTF-8 without U+200B and U+FFFD.
std::string EscapeSpecialTexts(
  std::string_view text, const std::vector<TextSpan>& spans, const std::vector<std::string>& special_texts
);

// Empty: the texts are fit for escaping, otherwise the reason.
std::string CheckSpecialTexts(const std::vector<std::string>& special_texts);

} // namespace giga_edge
