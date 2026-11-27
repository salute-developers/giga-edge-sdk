#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/question_rules.h"

#include "helpers.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <stdexcept>
#include <string>

using namespace giga_edge;
using namespace giga_edge::vits;
using Catch::Matchers::Contains;

namespace {

QuestionRules LanguageRules() {
  const auto path = "vits/synthetic/question_rules.json";
  return {ToString(CreateDataReader("data/")->ReadTransientData(path)), path};
}

// The spans as the text they cover, in the order found.
std::string Spans(const QuestionRules& rules, const std::string& plain) {
  std::string text;
  for (const auto& span : rules.Find(test::MakeSentence(plain, plain))) {
    text += (text.empty() ? "" : "|") + plain.substr(span.begin, span.size);
  }
  return text;
}

} // namespace

TEST_CASE("vits question rules: where a question rises", "[tts][vits]") {
  const auto rules = LanguageRules();
  // Not a question, nothing to mark.
  CHECK(Spans(rules, "").empty());
  CHECK(Spans(rules, "включил свет.").empty());
  CHECK(Spans(rules, "что включить.").empty());
  CHECK(Spans(rules, "?").empty());
  // The default: from the last space on; a question of one word gets nothing.
  CHECK(Spans(rules, "свет на кухне?") == " кухне?");
  CHECK(Spans(rules, "кухня?").empty());
  // Words and phrases; "ё" and "е" are one letter; words joined by '-' are one word.
  CHECK(Spans(rules, "какую температуру поставить в спальне?") == "какую");
  CHECK(Spans(rules, "еще свет?") == "еще");
  CHECK(Spans(rules, "о чем?") == "чем");
  CHECK(Spans(rules, "о чём?") == "чём");
  CHECK(Spans(rules, "так же светло?") == "так же");
  CHECK(Spans(rules, "что именно?") == "что|что именно");
  CHECK(Spans(rules, "кто-то пришёл?") == " пришёл?");
  CHECK(Spans(rules, "свет-кто?").empty());
  // A word before the last comma keeps the default, after it cancels it.
  CHECK(Spans(rules, "что, свет на кухне?") == "что| кухне?");
  CHECK(Spans(rules, "свет на кухне, что?") == "что");
  // The start of the sentence, a prefix of the first word.
  CHECK(Spans(rules, "включиться бы?") == "включить");
  CHECK(Spans(rules, "включить, свет?") == "включить| свет?");
  CHECK(Spans(rules, "свет включить?") == " включить?");
  // The word before a particle, after any separator; the particle is a whole word.
  CHECK(Spans(rules, "ты хочешь включить свет или выключить?") == "хочешь|свет");
  CHECK(Spans(rules, "свет, или нет?") == "свет");
  CHECK(Spans(rules, "свет ли горит?") == "свет");
  CHECK(Spans(rules, "или нет?") == " нет?");
  CHECK(Spans(rules, "это лишь шутка?") == " шутка?");
  // A word right before '?'.
  CHECK(Spans(rules, "хорошо?") == "хорошо");
  // Marks stand close to the word in the plain text, as in the marked one.
  CHECK(Spans(rules, "хорошо ?") == " ?");
  CHECK(Spans(rules, "хорошо, а потом?") == "а потом");
  CHECK(Spans(rules, "так?") == "так");
  CHECK(Spans(rules, "так ли?") == "так");
  CHECK(Spans(rules, "хорошо бы?") == " бы?");
  // "а" with the last word: after a space or a comma; with a one-letter word anywhere.
  CHECK(Spans(rules, "свет, а ты?") == "а ты");
  CHECK(Spans(rules, "свет,а ты?") == "а ты");
  CHECK(Spans(rules, "а ты?") == " ты?");
  CHECK(Spans(rules, "свет (а ты?)") == " ты?)");
  CHECK(Spans(rules, "а в спальне?") == "а в спальне");
  CHECK(Spans(rules, "свет (а в спальне?)") == "а в спальне");
  CHECK(Spans(rules, "свет, а в спальне?") == "а в спальне");
  CHECK(Spans(rules, "а во дворе?") == " дворе?");
  CHECK(Spans(rules, "свет, а ты, мама?") == " мама?");
}

TEST_CASE("vits question rules: phrases and words as given", "[tts][vits]") {
  const QuestionRules rules(
    R"({"words": ["так же"], "before_particles": ["ли"], "contrast_conjunctions": ["но"]})", "x"
  );
  CHECK(Spans(rules, "так  же?") == " же?");
  CHECK(Spans(rules, "так же?") == "так же");
  CHECK(Spans(rules, "так жена?") == " жена?");
  CHECK(Spans(rules, "свет, но ты?") == "но ты");
  CHECK(Spans(rules, "ли?").empty());
}

TEST_CASE("vits question rules: broken rules or words throw", "[tts][vits]") {
  CHECK_THROWS_WITH(QuestionRules("[]", "bad"), Contains("'bad': must be a JSON object"));
  CHECK_THROWS_WITH(QuestionRules("{", "bad"), Contains("not valid JSON"));
  CHECK_THROWS_WITH(QuestionRules(R"({"word": []})", "bad"), Contains("unknown list 'word'"));
  CHECK_THROWS_WITH(QuestionRules(R"({"words": "где"})", "bad"), Contains("'words' must be an array of strings"));
  CHECK_THROWS_WITH(QuestionRules(R"({"words": [1]})", "bad"), Contains("must hold non-empty strings"));
  CHECK_THROWS_WITH(QuestionRules(R"({"words": [""]})", "bad"), Contains("must hold non-empty strings"));
  CHECK_NOTHROW(QuestionRules("{}", "empty"));

  const auto rules = LanguageRules();
  auto sentence = test::MakeSentence("где свет?", "гдЕ свЕт?");
  sentence.words[1].plain.size = 20;
  CHECK_THROWS_WITH(rules.Find(sentence), Contains("word 1 is out of order or out of its text"));
  sentence = test::MakeSentence("где свет?", "гдЕ свЕт?");
  std::swap(sentence.words[0], sentence.words[1]);
  CHECK_THROWS_WITH(rules.Find(sentence), Contains("word 1 is out of order"));
  sentence = test::MakeSentence("где свет?", "гдЕ свЕт?");
  sentence.words[0].marked.begin = 1;
  CHECK_THROWS_WITH(rules.Find(sentence), Contains("word 0 is out of order or out of its text"));
}
