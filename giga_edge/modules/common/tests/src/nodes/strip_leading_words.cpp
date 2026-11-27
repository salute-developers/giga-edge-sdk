#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/strip_leading_words/node.h"

#include <catch.hpp>

#include <stdexcept>

using namespace giga_edge;

namespace {

std::string Strip(NodeContext& node, const std::string& text) {
  NodePush(node, "input", text);
  return NodeGet<std::string>(node, "output");
}

std::string Strip(NodeContext& node, const std::string& text, const std::string& words) {
  NodePush(node, "input", text);
  NodePush(node, "words", words);
  return NodeGet<std::string>(node, "output");
}

} // namespace

TEST_CASE("Strip leading words: configured word with punctuation", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [джарвис]");

  CHECK(Strip(node, "Джарвис, привет, как дела?") == "привет, как дела?");
  CHECK(Strip(node, "«Джарвис» — привет") == "привет");
  CHECK(Strip(node, "Джарвис.").empty());
  CHECK(Strip(node, "").empty());
}

TEST_CASE("Strip leading words: text without a leading match is untouched", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [джарвис]");

  CHECK(Strip(node, "  Привет, Джарвис  ") == "  Привет, Джарвис  ");
  CHECK(Strip(node, "Скажи джарвис") == "Скажи джарвис");
  CHECK(Strip(node, "Джарвисович пришёл") == "Джарвисович пришёл");
  CHECK(Strip(node, "2 джарвис") == "2 джарвис");
}

TEST_CASE("Strip leading words: restores the capital letter of the sentence", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [джарвис, jarvis]\ncapitalize_rest: true");

  CHECK(Strip(node, "Джарвис, привет, как дела?") == "Привет, как дела?");
  CHECK(Strip(node, "джарвис, привет") == "привет");
  CHECK(Strip(node, "Джарвис, ёлка") == "Ёлка");
  CHECK(Strip(node, "Jarvis, hello") == "Hello");
  CHECK(Strip(node, "Джарвис, 5 минут") == "5 минут");
}

TEST_CASE("Strip leading words: repeats", "[runtime][nodes]") {
  auto repeat = CreateNode("strip_leading_words", "words: [джарвис]");
  CHECK(Strip(repeat, "Джарвис, джарвис, привет") == "привет");

  auto once = CreateNode("strip_leading_words", "words: [джарвис]\nrepeat: false");
  CHECK(Strip(once, "Джарвис, джарвис, привет") == "джарвис, привет");
}

TEST_CASE("Strip leading words: one typo per so many letters", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [джарвис, да, алиса]\ntypos_per_letters: 3");

  CHECK(Strip(node, "Жарвис, привет") == "привет");
  CHECK(Strip(node, "Джалис, напомни") == "напомни");
  CHECK(Strip(node, "Жарвиз привет") == "привет");
  CHECK(Strip(node, "Жарит лето") == "Жарит лето");
  CHECK(Strip(node, "Джаро Ханза") == "Джаро Ханза");
  CHECK(Strip(node, "Олиса, включи") == "включи");
  CHECK(Strip(node, "Олеся, включи") == "Олеся, включи");
  CHECK(Strip(node, "На месте") == "На месте");
  CHECK(Strip(node, "Да, конечно") == "конечно");

  auto exact = CreateNode("strip_leading_words", "words: [джарвис]");
  CHECK(Strip(exact, "Жарвис, привет") == "Жарвис, привет");
}

TEST_CASE("Strip leading words: latin and cyrillic spellings of one word", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [джарвис, Alexa]\ntypos_per_letters: 4");

  CHECK(Strip(node, "Jarvis, привет") == "привет");
  CHECK(Strip(node, "JARVIS привет") == "привет");
  CHECK(Strip(node, "Алекса, включи свет") == "включи свет");
  CHECK(Strip(node, "Джарвиз привет") == "привет");
  CHECK(Strip(node, "Jarvisko привет") == "Jarvisko привет");
  CHECK(Strip(node, "Жарит привет") == "Жарит привет");

  auto exact = CreateNode("strip_leading_words", "words: [джарвис]");
  CHECK(Strip(exact, "Jarvis, привет") == "привет");
  CHECK(Strip(exact, "Jarvas, привет") == "Jarvas, привет");
}

TEST_CASE("Strip leading words: drops a text that does not start with the expected words", "[runtime][nodes]") {
  auto configured = CreateNode("strip_leading_words", "words: [джарвис]\ndrop_unmatched: true");
  CHECK(Strip(configured, "Джарвис, привет") == "привет");
  CHECK(Strip(configured, "Jarvis, привет") == "привет");
  CHECK(Strip(configured, "Жизнь Чарли Клауда").empty());

  auto supplied = CreateNode("strip_leading_words", "drop_unmatched: true", {}, {"words"});
  CHECK(Strip(supplied, "Джарвис, привет", "джарвис") == "привет");
  CHECK(Strip(supplied, "Жарит лето", "джарвис").empty());
  CHECK(Strip(supplied, "Просто текст по клавише", "") == "Просто текст по клавише");
}

TEST_CASE("Strip leading words: a few tokens before the word go with it", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [макс]\nmax_skipped_tokens: 2\ncapitalize_rest: true");

  CHECK(Strip(node, "Э-э, Макс, какие деньги в Ингушетии?") == "Какие деньги в Ингушетии?");
  CHECK(Strip(node, "Ну вот, Макс, привет") == "Привет");
  CHECK(Strip(node, "Скажи ей, что Макс придёт") == "Скажи ей, что Макс придёт");
  CHECK(Strip(node, "Макс, макс, привет") == "Привет");
  CHECK(Strip(node, "Привет, макс, ну, макс, пока") == "Ну, макс, пока");
}

TEST_CASE("Strip leading words: a word glued to the next one", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [маяк, джарвис]\nsplit_glued: true\ncapitalize_rest: true");

  CHECK(Strip(node, "Маякбаланс на карте Visa.") == "Баланс на карте Visa.");
  CHECK(Strip(node, "Джарвисне Павловне") == "Не Павловне");
  CHECK(Strip(node, "Jarvisнапомни") == "Напомни");
  CHECK(Strip(node, "Маяки, привет") == "Маяки, привет");
  CHECK(Strip(node, "Майк пришёл") == "Майк пришёл");
  CHECK(Strip(node, "Джарвис, маякбанк открыт") == "Маякбанк открыт");

  auto typos = CreateNode("strip_leading_words", "words: [ира]\nsplit_glued: true\ntypos_per_letters: 3");
  CHECK(Strip(typos, "Ирина пришла") == "Ирина пришла");

  auto off = CreateNode("strip_leading_words", "words: [маяк]");
  CHECK(Strip(off, "Маякбаланс на карте") == "Маякбаланс на карте");
}

TEST_CASE("Strip leading words: voice session filter", "[runtime][nodes]") {
  auto node = CreateNode(
    "strip_leading_words",
    "max_skipped_tokens: 2\nsplit_glued: true\ntypos_per_letters: 3\ncapitalize_rest: true\ndrop_unmatched: true",
    {},
    {"words"}
  );

  CHECK(Strip(node, "Джарвис, напиши Маше", "Джарвис.,Джарвис") == "Напиши Маше");
  CHECK(Strip(node, "Э-э, Жарвис, привет", "Джарвис.,Джарвис") == "Привет");
  CHECK(Strip(node, "Джарвисне Павловне Шабуровой.", "Джарвис.,Джарвис") == "Павловне Шабуровой.");
  CHECK(Strip(node, "Джарвиснапиши Маше", "Джарвис.,Джарвис") == "Напиши Маше");
  CHECK(Strip(node, "Жизнь Чарли Клауда.", "Джарвис.,Джарвис").empty());
  CHECK(Strip(node, "Жарит лето.", "Джарвис.,Джарвис").empty());
  CHECK(Strip(node, "Джаро Ханза. Жуазейру.", "Джарвис.,Джарвис").empty());
  CHECK(Strip(node, "Текст по клавише", "") == "Текст по клавише");
}

TEST_CASE("Strip leading words: assistant query after an activation word", "[runtime][nodes]") {
  auto node = CreateNode(
    "strip_leading_words",
    "words: [ветер, маяк, искра, макс]\nhold_words: true\nrepeat: false\ntypos_per_letters: 3\nsplit_glued: true",
    {},
    {"words"}
  );

  CHECK(Strip(node, "ветер включи свет", "") == "включи свет");
  CHECK(Strip(node, "Ветер, включи свет на кухне.", "") == "включи свет на кухне.");
  CHECK(Strip(node, "маяк выключи лампу", "") == "выключи лампу");
  CHECK(Strip(node, "Max, какая температура", "") == "какая температура");
  CHECK(Strip(node, "ветер сделай ярче", "") == "сделай ярче");
  CHECK(Strip(node, "ветервключи свет", "") == "включи свет");
  CHECK(Strip(node, "вотер включи свет", "") == "включи свет");

  CHECK(Strip(node, "включи маяк розетку", "") == "включи маяк розетку");
  CHECK(Strip(node, "ветер маяк бум сделай громче", "") == "маяк бум сделай громче");
  CHECK(Strip(node, "свет на кухне", "") == "свет на кухне");
  CHECK(Strip(node, "искры", "").empty());
  CHECK(Strip(node, "", "").empty());

  CHECK(Strip(node, "", " Джарвис.").empty());
  CHECK(Strip(node, "джарвис закрой шторы", "") == "закрой шторы");
  CHECK(Strip(node, "ветер закрой шторы", "") == "закрой шторы");
}

TEST_CASE("Strip leading words: letter ё matches е", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [Алёна]");

  CHECK(Strip(node, "Алена, привет") == "привет");
  CHECK(Strip(node, "АЛЁНА привет") == "привет");
}

TEST_CASE("Strip leading words: multi-word entries win over their prefixes", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [ветер, ветер маяк]\nrepeat: false");

  CHECK(Strip(node, "Ветер, маяк, включи свет") == "включи свет");
  CHECK(Strip(node, "Ветер, включи свет") == "включи свет");
  CHECK(Strip(node, "Маяк, включи свет") == "Маяк, включи свет");
}

TEST_CASE("Strip leading words: words supplied by input", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "", {}, {"words"});

  CHECK(Strip(node, "Маша, привет", "джарвис,маша") == "привет");
  CHECK(Strip(node, "Джарвис, привет", "джарвис,маша") == "привет");
  CHECK(Strip(node, "Маша, привет", "") == "Маша, привет");
}

TEST_CASE("Strip leading words: held words outlive the tick they came with", "[runtime][nodes]") {
  auto held = CreateNode("strip_leading_words", "words: [ветер]\nhold_words: true", {}, {"words"});
  CHECK(Strip(held, "", " Джарвис.").empty());
  CHECK(Strip(held, "джарвис включи свет", "") == "включи свет");
  CHECK(Strip(held, "ветер включи свет", "") == "включи свет");
  CHECK(Strip(held, "", "маша").empty());
  CHECK(Strip(held, "джарвис включи свет", "") == "джарвис включи свет");
  CHECK(Strip(held, "маша включи свет", "") == "включи свет");

  auto plain = CreateNode("strip_leading_words", "words: [ветер]", {}, {"words"});
  CHECK(Strip(plain, "", " Джарвис.").empty());
  CHECK(Strip(plain, "джарвис включи свет", "") == "джарвис включи свет");
}

TEST_CASE("Strip leading words: configured and supplied words together", "[runtime][nodes]") {
  auto node = CreateNode("strip_leading_words", "words: [ветер]\nwords_delimiter: ';'", {}, {"words"});

  CHECK(Strip(node, "Ветер, джарвис, привет", "джарвис;маша") == "привет");
}

namespace giga_edge {

inline bool operator==(const StripLeadingWordsNodeParams& lhs, const StripLeadingWordsNodeParams& rhs) {
  return lhs.words == rhs.words && lhs.words_delimiter == rhs.words_delimiter && lhs.repeat == rhs.repeat
    && lhs.typos_per_letters == rhs.typos_per_letters && lhs.max_skipped_tokens == rhs.max_skipped_tokens
    && lhs.split_glued == rhs.split_glued && lhs.capitalize_rest == rhs.capitalize_rest
    && lhs.drop_unmatched == rhs.drop_unmatched && lhs.hold_words == rhs.hold_words;
}

} // namespace giga_edge

TEST_CASE("Strip leading words, config parser", "[runtime][nodes]") {
  CHECK(ParseConfig<StripLeadingWordsNodeParams>(YAML::Load("")) == StripLeadingWordsNodeParams{});

  CHECK(
    ParseConfig<StripLeadingWordsNodeParams>(YAML::Load(R"(
      words: [ветер, джарвис]
      words_delimiter: ";"
      repeat: false
      typos_per_letters: 3
      max_skipped_tokens: 2
      split_glued: true
      capitalize_rest: true
      drop_unmatched: true
      hold_words: true
    )"))
    == StripLeadingWordsNodeParams{
      .words = {"ветер", "джарвис"},
      .words_delimiter = ';',
      .repeat = false,
      .typos_per_letters = 3,
      .max_skipped_tokens = 2,
      .split_glued = true,
      .capitalize_rest = true,
      .drop_unmatched = true,
      .hold_words = true,
    }
  );

  CHECK_THROWS_AS(ParseConfig<StripLeadingWordsNodeParams>(YAML::Load("words_delimiter: ', '")), std::invalid_argument);
  CHECK_THROWS_AS(ParseConfig<StripLeadingWordsNodeParams>(YAML::Load("typos_per_letters: -1")), std::invalid_argument);
  CHECK_THROWS_AS(
    ParseConfig<StripLeadingWordsNodeParams>(YAML::Load("max_skipped_tokens: -1")), std::invalid_argument
  );
}
