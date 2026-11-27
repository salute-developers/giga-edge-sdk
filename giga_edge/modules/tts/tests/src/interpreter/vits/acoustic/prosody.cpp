#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/prosody.h"

#include "helpers.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;
using Catch::Matchers::Contains;

namespace {

constexpr int kClasses = 6;
constexpr int kFeatures = 5;

struct Case {
  std::string plain;
  std::string marked;
  std::vector<std::string> tokens;
  std::vector<std::string> labels; // the argmax of each token over the first embedding_dims classes, a digit a feature
  std::string q_labels;            // a digit a label, rows one after another
};

// One-hot qf0_logits by the labels of each token; the class past embedding_dims is the largest and must not count.
std::vector<float> Logits(const std::vector<std::string>& labels) {
  std::vector<float> logits(labels.size() * kClasses * kFeatures, 0.0F);
  for (size_t token = 0; token < labels.size(); ++token) {
    for (size_t feature = 0; feature < kFeatures; ++feature) {
      const auto at = [&](size_t label) -> float& {
        return logits[(token * kClasses + label) * kFeatures + feature];
      };
      at(static_cast<size_t>(labels[token][feature] - '0')) = 1.0F;
      at(kClasses - 1) = 9.0F;
    }
  }
  return logits;
}

std::string Digits(const std::vector<int64_t>& labels) {
  std::string text;
  for (const auto label : labels) {
    text += std::to_string(label);
  }
  return text;
}

std::string Repeat(const std::string& text, int times) {
  std::string result;
  for (int i = 0; i < times; ++i) {
    result += text;
  }
  return result;
}

QuestionRules LanguageRules() {
  const auto path = "vits/synthetic/question_rules.json";
  return {ToString(CreateDataReader("data/")->ReadTransientData(path)), path};
}

std::vector<int64_t> Labels(
  const PreparedSentence& sentence,
  const std::vector<std::string>& tokens,
  const std::vector<std::string>& labels,
  const VoiceConfig& voice,
  const QuestionRules* rules
) {
  const auto logits = Logits(labels);
  const std::vector<int> shape{1, static_cast<int>(tokens.size()), kClasses, kFeatures};
  return MakeProsodyLabels(sentence, test::MakeTokens(sentence.plain, tokens), logits.data(), shape, voice, rules);
}

} // namespace

TEST_CASE("vits prosody: the labels of the pipeline on reference phrases", "[tts][vits]") {
  const auto voice = test::TestVoice();
  const auto rules = LanguageRules();
  const std::vector<Case> cases = {
    // A statement.
    {"включил свет на кухне.",
     "включИл свЕт нА кУхне.",
     {"включил", "свет", "на", "кухне", "."},
     {"34142", "32024", "32010", "00113", "10323"},
     "5555524142241422414224142241422414224142241422414224142241422414224142241425555555555220242202422024220242202422"
     "0242202422024555555555522010220102201022010555555555520113201132011320113201132011320113201132011320113555555555"
     "5"},
    // A pause and a comma.
    {"в гостиной пятнадцать целых, шесть десятых градуса.",
     "в гостИной пятнАдцать цЕлых^, шЕсть десЯтых грАдуса.",
     {"в", "гостиной", "пятнадцать", "целых", ",", "шесть", "десятых", "градуса", "."},
     {"01234", "33311", "12220", "24402", "10323", "24211", "12221", "10133", "10323"},
     "55555212342123455555555552331123311233112331123311233112331123311233112331123311233112331123311233112331155555555"
     "55222202222022220222202222022220222202222022220222202222022220222202222022220222202222022220222202222055555555552"
     "44022440224402244022440224402244022440224402244025555555555555555555555555555552421124211242112421124211242112421"
     "12421124211242115555555555222212222122221222212222122221222212222122221222212222122221222212222155555555552013320"
     "1332013320133201332013320133201332013320133201332013320133201335555555555"},
    // A question.
    {"ты хочешь включить свет или выключить?",
     "тЫ хОчешь включИть свЕт Или вЫключить?",
     {"ты", "хочешь", "включить", "свет", "или", "выключить", "?"},
     {"32212", "32330", "23110", "34444", "43132", "22113", "10323"},
     "55555222122221222212222125555555555344403444034440344403444034440344403444034440344403444034440555555555523110231"
     "10231102311023110231102311023110231102311023110231102311023110231102311055555555553444434444344443444434444344443"
     "44443444455555555552313223132231322313223132231325555555555221132211322113221132211322113221132211322113221132211"
     "3221132211322113221132211322113221135555555555"},
    // A capital Ё and word pieces.
    {"ёлка на балконе уже светится.",
     "Ёлка нА балкОне ужЕ свЕтится.",
     {"ёл", "##ка", "на", "балкон", "##е", "уже", "светит", "##ся", "."},
     {"33424", "33424", "31220", "33212", "33212", "22141", "10123", "10123", "10323"},
     "55555234242342423424234242342423424234242342455555555552122021220212202122055555555552321223212232122321223212232"
     "12232122321223212232122321223212232122321255555555552214122141221412214122141221415555555555201232012320123201232"
     "012320123201232012320123201232012320123201232012320123201235555555555"},
  };
  for (const auto& c : cases) {
    INFO(c.marked);
    const auto sentence = test::MakeSentence(c.plain, c.marked);
    CHECK(Digits(Labels(sentence, c.tokens, c.labels, voice, &rules)) == c.q_labels);
  }

  // Without the rules, or with a voice that does not use them, a question has no question labels.
  const auto& question = cases[2];
  const auto sentence = test::MakeSentence(question.plain, question.marked);
  const auto full = Labels(sentence, question.tokens, question.labels, voice, &rules);
  const auto plain = Labels(sentence, question.tokens, question.labels, voice, nullptr);
  CHECK(plain != full);
  CHECK(Digits(plain).substr(0, 15) == "555552221222212");
  CHECK(Digits(plain).substr(35, 10) == "2233022330");
  auto off = voice;
  off.use_question_rules = false;
  CHECK(Labels(sentence, question.tokens, question.labels, off, &rules) == plain);

  // Without blanks: a row a symbol.
  off.blank_intersperse = false;
  const auto& statement = cases[0];
  CHECK(
    Digits(
      Labels(test::MakeSentence(statement.plain, statement.marked), statement.tokens, statement.labels, off, &rules)
    )
    == Repeat("24142", 7) + "55555" + Repeat("22024", 4) + "55555" + Repeat("22010", 2) + "55555" + Repeat("20113", 5)
      + "55555"
  );
}

TEST_CASE("vits prosody: letters of words of different length", "[tts][vits]") {
  const auto voice = test::TestVoice();
  const std::vector<std::string> labels{"01111", "03333", "04444"};
  const auto t0 = std::string("21111");
  const auto t1 = std::string("23333");
  const auto none = std::string("55555");
  // A longer marked word: 3 plain letters for 5, a shorter one: 5 for 3.
  CHECK(
    Digits(Labels(test::MakeSentence("кот.", "кОотт."), {"ко", "##т", "."}, labels, voice, nullptr))
    == none + Repeat(t0, 8) + Repeat(t1, 2) + Repeat(none, 2)
  );
  CHECK(
    Digits(Labels(test::MakeSentence("котик.", "кОт."), {"ко", "##тик", "."}, labels, voice, nullptr))
    == none + Repeat(t0, 4) + Repeat(t1, 2) + Repeat(none, 2)
  );
  // Other letters, as many of them: letter by letter.
  CHECK(
    Digits(Labels(test::MakeSentence("интернет.", "интэрнЭт."), {"интер", "##нет", "."}, labels, voice, nullptr))
    == none + Repeat(t0, 10) + Repeat(t1, 6) + Repeat(none, 2)
  );
  // Every 'ё' is a capital, the plain text may have 'е': the letters still pair by count.
  CHECK(
    Digits(Labels(test::MakeSentence("четырехсот", "четырЁхсОт"), {"четырех", "##сот"}, labels, voice, nullptr))
    == none + Repeat(t0, 14) + Repeat(t1, 6)
  );
  // A word without letters in plain gives its marked letters no labels.
  const PreparedSentence empty_plain{"- кот", "кот кот", {{{0, 1}, {0, 6}}, {{2, 6}, {7, 6}}}};
  CHECK(
    Digits(Labels(empty_plain, {"-", "кот"}, labels, voice, nullptr))
    == none + Repeat(none, 6) + Repeat(none, 2) + Repeat(std::string("23333"), 6)
  );
}

TEST_CASE("vits prosody: edges", "[tts][vits]") {
  const auto voice = test::TestVoice();
  const auto rules = LanguageRules();
  const std::vector<std::string> labels{"01111", "03333", "04444"};
  const auto t0 = std::string("21111");
  const auto none = std::string("55555");
  // Nothing at all, only marks.
  CHECK(Digits(MakeProsodyLabels({}, {}, nullptr, {1, 0, kClasses, kFeatures}, voice, &rules)) == none);
  CHECK(Digits(Labels(test::MakeSentence("?", "?"), {"?"}, labels, voice, &rules)) == Repeat(none, 3));
  CHECK(Digits(Labels(test::MakeSentence("?!", "?!"), {"?", "!"}, labels, voice, &rules)) == Repeat(none, 5));
  // Pauses at the start, at the end and in a row.
  CHECK(
    Digits(Labels(test::MakeSentence("свет.", "^свЕт^^."), {"свет", "."}, labels, voice, &rules))
    == Repeat(none, 3) + Repeat(t0, 8) + Repeat(none, 6)
  );
  // One word, a word of no vowels, an unknown word.
  CHECK(Digits(Labels(test::MakeSentence("в", "в"), {"в"}, labels, voice, &rules)) == none + Repeat(t0, 2));
  CHECK(
    Digits(Labels(test::MakeSentence("в дом.", "в дОм."), {"в", "[UNK]", "."}, labels, voice, &rules))
    == none + Repeat(t0, 2) + Repeat(none, 10)
  );
  // A one-word question has no question labels; a long word is fine.
  CHECK(
    Digits(Labels(test::MakeSentence("свет?", "свЕт?"), {"свет", "?"}, labels, voice, &rules))
    == none + Repeat(t0, 8) + Repeat(none, 2)
  );
  std::string long_plain;
  std::string long_marked;
  for (int i = 0; i < 200; ++i) {
    long_plain += "а";
    long_marked += i == 100 ? "А" : "а";
  }
  CHECK(
    Digits(Labels(test::MakeSentence(long_plain, long_marked), {"[UNK]"}, labels, voice, &rules)) == Repeat(none, 401)
  );
}

TEST_CASE("vits prosody: broken input throws", "[tts][vits]") {
  const auto voice = test::TestVoice();
  const std::vector<std::string> labels{"01111", "03333"};
  const auto logits = Logits(labels);
  const auto sentence = test::MakeSentence("свет.", "свЕт.");
  const auto tokens = test::MakeTokens(sentence.plain, {"свет", "."});
  const auto run = [&](const PreparedSentence& s, const std::vector<WordPieceToken>& t, const std::vector<int>& shape) {
    return MakeProsodyLabels(s, t, logits.data(), shape, voice, nullptr);
  };
  const std::vector<int> shape{1, 2, kClasses, kFeatures};
  CHECK_NOTHROW(run(sentence, tokens, shape));

  CHECK_THROWS_WITH(run(sentence, tokens, {2, kClasses, kFeatures}), Contains("qf0_logits of shape [2, 6, 5]"));
  CHECK_THROWS_WITH(run(sentence, tokens, {1, 3, kClasses, kFeatures}), Contains("expected [1, 2, classes"));
  CHECK_THROWS_WITH(run(sentence, tokens, {1, 2, 4, kFeatures}), Contains("classes >= embedding_dims"));
  CHECK_THROWS_WITH(run(sentence, tokens, {1, 2, kClasses, 4}), Contains("qf0_logits of shape [1, 2, 6, 4]"));
  auto no_features = voice;
  no_features.embedding_dims.clear();
  CHECK_THROWS_WITH(
    MakeProsodyLabels(sentence, tokens, logits.data(), {1, 2, kClasses, 0}, no_features, nullptr),
    Contains("qf0_logits of shape [1, 2, 6, 0]")
  );

  auto outside = sentence;
  outside.words.clear();
  CHECK_THROWS_WITH(run(outside, tokens, shape), Contains("a letter of the marked text is outside the words"));
  auto out_of_text = sentence;
  out_of_text.words[0].marked.size = 10;
  CHECK_THROWS_WITH(run(out_of_text, tokens, shape), Contains("word 0 is out of order or out of its text"));
  auto mid_letter = sentence;
  mid_letter.words[0].plain.size = 7;
  CHECK_THROWS_WITH(run(mid_letter, tokens, shape), Contains("word 0"));
  auto bad_token = tokens;
  bad_token[1].range = {9, 1};
  CHECK_THROWS_WITH(run(sentence, bad_token, shape), Contains("token 1 is out of the plain text"));
  auto broken = sentence;
  broken.marked = "свЕт\xD0";
  CHECK_THROWS_WITH(run(broken, tokens, shape), Contains("invalid UTF-8 at byte 8"));
}
