#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"

#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// Where a question gets the question intonation: rules over the words of the plain text. Words joined by a bare '-'
// count as one ("кто-то"); 'ё' and 'е' are the same letter. The lists come from a JSON file of the language:
//   words                 a word or a phrase of words separated by single spaces ("что именно");
//   sentence_starts       the text starts with it ("включить" in "включить свет?");
//   before_particles      the word right before such a particle gets it ("свет" in "свет или нет?");
//   before_question_mark  the word right before '?' ("хорошо?");
//   contrast_conjunctions the conjunction with the word right before '?': after a space or a comma ("..., а ты?"),
//                         or with a one-letter word in between anywhere ("а в спальне?").
// A match of the first two kinds with no comma after it, or any match of the others, cancels the default: the text
// from the last space on, that is the last word.
// Plain word matching instead of regular expressions: a particle is a whole word ("лишь" is not "ли") after any
// separator, and every repeat of a word counts.
class QuestionRules {
public:
  QuestionRules(std::string_view json, const std::string& name);

  // Byte ranges of sentence.plain; none when the plain text has no '?'.
  std::vector<TextRange> Find(const PreparedSentence& sentence) const;

private:
  std::vector<std::string> words_;
  std::vector<std::string> sentence_starts_;
  std::vector<std::string> before_particles_;
  std::vector<std::string> before_question_mark_;
  std::vector<std::string> contrast_conjunctions_;
};

} // namespace giga_edge::vits
