#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/question_rules.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"

#include <cstdint>
#include <vector>

namespace giga_edge::vits {

// q_labels [InputLength(symbols of marked), features] row by row. tokens: WordPiece of sentence.plain; qf0_logits:
// the text_qf0 output [1, tokens, classes, features].
// A letter of a marked word takes the token of a plain letter of the same word: the same by count when the words have
// as many letters, proportionally otherwise. Its labels are the argmax over the first embedding_dims classes of the
// token, then the forced labels and, where the question rules point, the question labels. Every other symbol, and a
// letter whose token is [UNK] or does not start with a letter, is "no value". With blank_intersperse every row repeats
// on the blank after it; the first blank is "no value".
// Throws std::invalid_argument on a letter of marked outside the words, bad words or tokens, or a wrong shape.
std::vector<int64_t> MakeProsodyLabels(
  const PreparedSentence& sentence,
  const std::vector<WordPieceToken>& tokens,
  const float* qf0_logits,
  const std::vector<int>& qf0_shape,
  const VoiceConfig& voice,
  const QuestionRules* question_rules
);

} // namespace giga_edge::vits
