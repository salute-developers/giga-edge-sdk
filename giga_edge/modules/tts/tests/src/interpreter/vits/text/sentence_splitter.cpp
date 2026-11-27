#include "giga_edge/modules/tts/interpreter/backends/vits/text/sentence_splitter.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalizer.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <catch.hpp>

#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;

namespace {

const TextNormalizer& Normalizer() {
  static const TextNormalizer normalizer(*CreateDataReader("data/vits"), "text/ru");
  return normalizer;
}

std::vector<std::string> Texts(const std::vector<NormalizedSentence>& sentences) {
  std::vector<std::string> texts;
  texts.reserve(sentences.size());
  for (const auto& sentence : sentences) {
    texts.push_back(sentence.text);
  }
  return texts;
}

std::vector<int> Pauses(const std::vector<NormalizedSentence>& sentences) {
  std::vector<int> pauses;
  pauses.reserve(sentences.size());
  for (const auto& sentence : sentences) {
    pauses.push_back(sentence.pause_after_ms);
  }
  return pauses;
}

// Code points without stress marks.
size_t Length(const std::string& text) {
  size_t length = 0;
  for (size_t pos = 0; pos < text.size(); ++pos) {
    length += IsUtf8Boundary(text, pos) && text[pos] != '+' ? 1 : 0;
  }
  return length;
}

// The letters and spaces only: what the sentences say, without the marks the cutting changes.
std::string Words(const std::string& text) {
  std::string words;
  for (size_t pos = 0; pos < text.size();) {
    const auto code_point = DecodeUtf8(text, pos);
    if (IsLetter(code_point.value) || text[pos] == '+') {
      words.append(text, pos, code_point.size);
    } else if (text[pos] == ' ' && !words.empty() && words.back() != ' ') {
      words += ' ';
    }
    pos += code_point.size;
  }
  return words;
}

std::string JoinedWords(const std::vector<NormalizedSentence>& sentences) {
  std::string joined;
  for (const auto& sentence : sentences) {
    joined += Words(sentence.text) + " ";
  }
  return joined;
}

} // namespace

TEST_CASE("vits sentences: a run of end marks is one boundary", "[tts][vits][sentences]") {
  CHECK(Texts(Normalizer().Normalize("Ты серьёзно?!")) == std::vector<std::string>{"ты серьёзно?"});
  CHECK(Texts(Normalizer().Normalize("Не может быть!!!")) == std::vector<std::string>{"не может быть!"});
  CHECK(
    Texts(Normalizer().Normalize("Подождите... Сейчас проверю."))
    == std::vector<std::string>{"подождите.", "сейчас проверю."}
  );
  const auto sentences = Normalizer().Normalize("Включил свет. Какую яркость? Готово!");
  CHECK(Texts(sentences) == std::vector<std::string>{"включил свет.", "какую яркость?", "готово!"});
  CHECK(Pauses(sentences) == std::vector<int>{500, 500, 0});
}

TEST_CASE("vits sentences: boundaries of D16", "[tts][vits][sentences]") {
  struct Expected {
    const char* text;
    std::vector<std::string> sentences;
    std::vector<int> pauses;
  };
  const Expected cases[] = {
    {"Он опоздал, т. е. не пришёл.", {"он опоздал, то +есть не пришёл."}, {0}},
    {"А. С. Пушкин родился в Москве.", {"а эс пушкин родился в москве."}, {0}},
    {"Это сказал Пушкин А. С.", {"это сказал пушкин а эс."}, {0}},
    {"«Ты придёшь?» — спросил он.", {"\"ты придёшь?\"", "спросил он."}, {500, 0}},
    {"(Привет!) Пока.", {"(привет!)", "пока."}, {500, 0}},
    {"Ну… давай.", {"ну. давай."}, {0}},
    {"Ну... давай.", {"ну. давай."}, {0}},
    {"Подождите... Сейчас проверю.", {"подождите.", "сейчас проверю."}, {500, 0}},
    {"Свет включён; дверь закрыта.", {"свет включён.", "дверь закрыта."}, {500, 0}},
    {"Включил. яркость 30%.", {"включил.", "яркость тр+идцать проц+ентов."}, {500, 0}},
    {"5 руб. Доставка завтра.", {"пять рубл+ей.", "доставка завтра."}, {500, 0}},
    {"Купи хлеб и т. д. Потом позвони.", {"купи хлеб и так д+алее.", "потом позвони."}, {500, 0}},
    {"Он живёт в г. Москва.", {"он живёт в г+ороде москва."}, {0}},
    {"Улица — ул. Ленина.", {"улица - +улица ленина."}, {0}},
    {"Пункт А. Далее.", {"пункт а.", "далее."}, {500, 0}},
    {"Буква А. Буква Б.", {"буква а.", "буква бэ."}, {500, 0}},
    {"Привет. А. Пушкин пришёл.", {"привет.", "а пушкин пришёл."}, {500, 0}},
    {"Пушкин А. пришёл.", {"пушкин а пришёл."}, {0}},
    {"Это а. Потом.", {"это а.", "потом."}, {500, 0}},
    {", - начало", {"начало."}, {0}},
  };
  for (const auto& expected : cases) {
    INFO(expected.text);
    const auto sentences = Normalizer().Normalize(expected.text);
    CHECK(Texts(sentences) == expected.sentences);
    CHECK(Pauses(sentences) == expected.pauses);
  }
}

TEST_CASE("vits sentences: no letters, no sentence", "[tts][vits][sentences]") {
  for (const auto* text : {"", "   ", "...", "—", "!!!!", ", , ; : - – —", "«»()\"", "\n\n"}) {
    INFO(text);
    CHECK(Normalizer().Normalize(text).empty());
  }
  // The mark of a dropped piece does not take the pause of the last sentence.
  CHECK(Pauses(Normalizer().Normalize("Привет. ... !")) == std::vector<int>{0});
  CHECK(Texts(Normalizer().Normalize("Привет, ")) == std::vector<std::string>{"привет."});
  CHECK(Texts(Normalizer().Normalize("5")) == std::vector<std::string>{"пять."});
}

TEST_CASE("vits sentences: a long sentence is cut at a clause mark, then at a space", "[tts][vits][sentences]") {
  SentenceParams params;
  params.max_sentence_chars = 30;
  const std::vector<Piece> pieces = {
    {"первая часть", PieceKind::WORD, false},
    {",", PieceKind::CLAUSE, false},
    {"вторая", PieceKind::WORD, true},
    {"часть", PieceKind::WORD, true},
    {"и ещё много слов", PieceKind::WORD, true},
    {"?", PieceKind::END, false},
  };
  const auto sentences = SplitSentences(pieces, params);
  CHECK(Texts(sentences) == std::vector<std::string>{"первая часть.", "вторая часть.", "и ещё много слов?"});
  CHECK(Pauses(sentences) == std::vector<int>{150, 20, 0});

  // The last clause mark within the limit; a mark without a space after it.
  const std::vector<Piece> clauses = {
    {"раз", PieceKind::WORD, false},
    {",", PieceKind::CLAUSE, false},
    {"два", PieceKind::WORD, true},
    {",", PieceKind::CLAUSE, false},
    {"три", PieceKind::WORD, false},
    {"четыре пять", PieceKind::WORD, true},
  };
  CHECK(Texts(SplitSentences(clauses, params)) == std::vector<std::string>{"раз, два.", "три четыре пять."});

  // A last piece without letters gives its end mark and pause to the piece before it.
  const auto tail = SplitSentences(
    {{"ааааааааааааааааааа", PieceKind::WORD, false},
     {")", PieceKind::MARK, false},
     {"?", PieceKind::END, false},
     {"дальше", PieceKind::WORD, true}},
    params
  );
  CHECK(Texts(tail) == std::vector<std::string>{"ааааааааааааааааааа)?", "дальше."});
  CHECK(Pauses(tail) == std::vector<int>{500, 0});

  // A sentence without letters gives nothing to the sentence before it.
  CHECK(
    Texts(SplitSentences(
      {{"привет", PieceKind::WORD, false},
       {".", PieceKind::END, false},
       {"(", PieceKind::MARK, true},
       {")", PieceKind::MARK, false},
       {"?", PieceKind::END, false}},
      params
    ))
    == std::vector<std::string>{"привет."}
  );
  // A group longer than a sentence is cut at its own spaces.
  CHECK(
    Texts(SplitSentences({{"раздва три четыре пятьдесят шесть", PieceKind::WORD, false}}, params))
    == std::vector<std::string>{"раздва три четыре.", "пятьдесят шесть."}
  );

  // Only the alphabet leaves, with the spaces tidied.
  CHECK(
    Texts(SplitSentences(
      {{"(", PieceKind::MARK, false}, {"ß привет", PieceKind::WORD, false}, {")", PieceKind::MARK, false}}, params
    ))
    == std::vector<std::string>{"(привет)."}
  );

  // A group of words is not cut if it fits a sentence.
  const std::vector<Piece> group = {
    {"а б в", PieceKind::WORD, false},
    {"один два три четыре", PieceKind::WORD, true},
  };
  CHECK(Texts(SplitSentences(group, params)) == std::vector<std::string>{"а б в.", "один два три четыре."});
  // A dash between words is a clause mark too.
  const std::vector<Piece> dash = {
    {"слово слово", PieceKind::WORD, false},
    {"-", PieceKind::CLAUSE, true},
    {"ещё", PieceKind::WORD, true},
    {"слово", PieceKind::WORD, true},
  };
  const auto dashed = SplitSentences(dash, params);
  CHECK(Texts(dashed) == std::vector<std::string>{"слово слово.", "ещё слово."});
  CHECK(Pauses(dashed) == std::vector<int>{150, 0});
}

TEST_CASE("vits sentences: the measure of the training data", "[tts][vits][sentences]") {
  SentenceParams params;
  params.max_sentence_chars = 30;
  // "раз, два три четыре": 5 + 3 + 6 + 6 + 9 = 29 within 30, with " ." 33 over it: cut at the comma.
  CHECK(
    Texts(SplitSentences(
      {{"раз", PieceKind::WORD, false}, {",", PieceKind::CLAUSE, false}, {"два три четыре", PieceKind::WORD, true}},
      params
    ))
    == std::vector<std::string>{"раз.", "два три четыре."}
  );
  // Without a clause mark only the end mark is over the limit: the whole text.
  CHECK(
    Texts(SplitSentences(
      {{"раз", PieceKind::WORD, false},
       {"два", PieceKind::WORD, true},
       {"три", PieceKind::WORD, true},
       {"четыре", PieceKind::WORD, true},
       {"а", PieceKind::WORD, true}},
      params
    ))
    == std::vector<std::string>{"раз два три четыре а."}
  );
  // No space is measured before a comma: "раз, а, а, а" is 26 and " ." 30, within the limit.
  CHECK(
    Texts(SplitSentences(
      {{"раз", PieceKind::WORD, false},
       {",", PieceKind::CLAUSE, false},
       {"а", PieceKind::WORD, true},
       {",", PieceKind::CLAUSE, false},
       {"а", PieceKind::WORD, true},
       {",", PieceKind::CLAUSE, false},
       {"а", PieceKind::WORD, true}},
      params
    ))
    == std::vector<std::string>{"раз, а, а, а."}
  );
  // Stress marks are not measured, each word is its letters and 2, a space before it 1.
  CHECK(
    Texts(SplitSentences(
      {{"р+аз", PieceKind::WORD, false},
       {"дв+а", PieceKind::WORD, true},
       {"тр+и", PieceKind::WORD, true},
       {"чет+ыре", PieceKind::WORD, true},
       {"п+ять", PieceKind::WORD, true},
       {"ш+есть", PieceKind::WORD, true}},
      params
    ))
    == std::vector<std::string>{"р+аз дв+а тр+и чет+ыре.", "п+ять ш+есть."}
  );
}

TEST_CASE("vits sentences: a word longer than a sentence is cut at the limit", "[tts][vits][sentences]") {
  std::string word;
  for (int index = 0; index < 350; ++index) {
    word += "а";
  }
  // By the measure: 178 letters and the 2 of the token are 180, the dot of a cut piece is not measured.
  const auto sentences = Normalizer().Normalize(word);
  REQUIRE(sentences.size() == 2);
  CHECK(Length(sentences[0].text) == 179);
  CHECK(Length(sentences[1].text) == 173);
  CHECK(Pauses(sentences) == std::vector<int>{20, 0});

  // Stress marks are not counted and stay before their vowel: 21 - 2 is 19 letters.
  SentenceParams params;
  params.max_sentence_chars = 21;
  const auto stressed = SplitSentences({{"аааааааааааааааааа+а+аааааа", PieceKind::WORD, false}}, params);
  CHECK(Texts(stressed) == std::vector<std::string>{"аааааааааааааааааа+а.", "+аааааа."});
}

TEST_CASE("vits sentences: a number with its unit is not cut", "[tts][vits][sentences]") {
  // The number crosses the limit of 180 characters at every shift.
  for (int shift = 0; shift < 60; ++shift) {
    std::string text;
    while (Length(text) < 140 + static_cast<size_t>(shift)) {
      text += "свет ";
    }
    text += "1 234 567 руб. и ещё немного слов в конце";
    INFO(text);
    const auto sentences = Normalizer().Normalize(text);
    REQUIRE(sentences.size() >= 2);
    const auto number = "милли+он дв+ести тр+идцать чет+ыре т+ысячи пятьс+от шестьдес+ят семь рубл+ей";
    int found = 0;
    for (const auto& sentence : sentences) {
      CHECK(Length(sentence.text) <= 180);
      found += sentence.text.find(number) != std::string::npos ? 1 : 0;
    }
    CHECK(found == 1);
  }
}

TEST_CASE("vits sentences: a long text loses and repeats nothing", "[tts][vits][sentences]") {
  const std::string sentence = "Свет на кухне включён, температура 22 градуса. ";
  const std::string spoken = "свет на кухне включён температура дв+адцать два градуса ";
  std::string text;
  std::string expected;
  std::string run_on;
  std::string run_on_expected;
  while (text.size() < 10000) {
    text += sentence;
    expected += spoken;
    run_on += "свет, свет и свет ";
    run_on_expected += "свет свет и свет ";
  }
  const auto sentences = Normalizer().Normalize(text);
  CHECK(sentences.size() * sentence.size() == text.size());
  CHECK(JoinedWords(sentences) == expected);

  // Without a single end mark.
  const auto cut = Normalizer().Normalize(run_on);
  CHECK(cut.size() > 10);
  CHECK(JoinedWords(cut) == run_on_expected);
  for (size_t index = 0; index < cut.size(); ++index) {
    CHECK(Length(cut[index].text) <= 180);
    CHECK(cut[index].text.back() == '.');
    CHECK(cut[index].pause_after_ms == (index + 1 == cut.size() ? 0 : 150));
  }
}

TEST_CASE("vits sentences: a number and its noun written in full are one group", "[tts][vits][sentences]") {
  SentenceParams params;
  params.max_sentence_chars = kMinSentenceChars;
  const TextNormalizer normalizer(*CreateDataReader("data/vits"), "text/ru", params);
  CHECK(
    Texts(normalizer.Normalize("Ждать осталось 2 минуты")) == std::vector<std::string>{"ждать осталось.", "две минуты."}
  );
}

TEST_CASE("vits sentences: parameters", "[tts][vits][sentences]") {
  SentenceParams params;
  params.max_sentence_chars = kMinSentenceChars - 1;
  CHECK_THROWS_AS(SplitSentences({}, params), std::invalid_argument);
  CHECK_THROWS_AS(TextNormalizer(*CreateDataReader("data/vits"), "text/ru", params), std::invalid_argument);
  params.max_sentence_chars = kMaxSentenceChars + 1;
  CHECK_THROWS_AS(SplitSentences({}, params), std::invalid_argument);
  params.max_sentence_chars = kMaxSentenceChars;
  CHECK_NOTHROW(SplitSentences({}, params));
  params.max_sentence_chars = kMinSentenceChars;
  CHECK_NOTHROW(SplitSentences({}, params));
  for (auto* pause : {&params.sentence_pause_ms, &params.clause_pause_ms, &params.word_pause_ms}) {
    *pause = -1;
    CHECK_THROWS_AS(SplitSentences({}, params), std::invalid_argument);
    *pause = kMaxPauseMs + 1;
    CHECK_THROWS_AS(SplitSentences({}, params), std::invalid_argument);
    *pause = kMaxPauseMs;
    CHECK_NOTHROW(SplitSentences({}, params));
    *pause = 0;
  }
  params.sentence_pause_ms = 7;
  const auto sentences = SplitSentences(
    {{"раз", PieceKind::WORD, false}, {".", PieceKind::END, false}, {"два", PieceKind::WORD, true}}, params
  );
  CHECK(Pauses(sentences) == std::vector<int>{7, 0});
}

namespace {

// The alphabet of NormalizedSentence: lower-case Russian letters, single spaces, .,;:?!-()" with .,;:?!) right after
// a word and no space after '(', '+' before a vowel; the end is .?! with closing quotes or brackets after it.
bool InAlphabet(const std::string& text) {
  const std::u32string vowels = U"аеёиоуыэюя";
  const std::string marks_list = ".,;:?!-()\"";
  std::u32string letters;
  for (size_t pos = 0; pos < text.size();) {
    const auto code_point = DecodeUtf8(text, pos);
    letters += code_point.value;
    pos += code_point.size;
  }
  if (letters.empty() || letters.front() == U' ' || letters.back() == U' ') {
    return false;
  }
  for (size_t index = 0; index < letters.size(); ++index) {
    const auto letter = letters[index];
    const auto russian = (letter >= U'а' && letter <= U'я') || letter == U'ё';
    const auto mark = letter < 0x80 && marks_list.find(static_cast<char>(letter)) != std::string::npos;
    if (letter == U'+') {
      if (index + 1 == letters.size() || vowels.find(letters[index + 1]) == std::u32string::npos) {
        return false;
      }
    } else if (letter == U' ') {
      if (
        letters[index + 1] == U' ' || std::u32string(U".,;:?!)").find(letters[index + 1]) != std::u32string::npos
        || letters[index - 1] == U'('
      ) {
        return false;
      }
    } else if (!russian && !mark) {
      return false;
    }
  }
  auto end = letters.size();
  while (end > 0 && (letters[end - 1] == U'"' || letters[end - 1] == U')')) {
    --end;
  }
  return end > 0 && std::u32string(U".?!").find(letters[end - 1]) != std::u32string::npos;
}

} // namespace

TEST_CASE("vits sentences: only the alphabet of the contract leaves the normalizer", "[tts][vits][sentences]") {
  const std::vector<std::string> fragments = {
    "Свет",       "включён", " ",       "  ",       "\t",     "\n",     ".",
    ",",          ";",       ":",       "?",        "!",      "-",      "—",
    "…",          "«",       "»",       "\"",       "'",      "(",      ")",
    "[",          "]",       "{",       "}",        "<b>",    "</b>",   "&amp;",
    "&",          "*",       "**",      "_",        "~",      "#",      "^",
    "`",          "\\",      "/",       "|",        "@",      "%",      "$",
    "€",          "₽",       "°",       "№",        "+",      "+а",     "з+амок",
    "+7",         "2+2",     "5",       "21",       "1 234",  "3,5",    "12:30",
    "12.04.2024", "5-7",     "1/2",     "XIV",      "Wi-Fi",  "null",   "USB",
    "МЧС",        "т. е.",   "ул.",     "г.",       "А.",     "cвeт",   "Ёлка",
    "Їжак",       "ў",       "你好",    "😀",       "✅",     "\xff",   "\xc3",
    "\xe2\x82",   "\x01",    "\x1b[1m", "\u0301",   "\u00a0", "\u200b", "www.example.ru",
    "a@b.com",    "lamp",    "THE",     "Coldplay", "ß",      "é",      "Ω",
  };
  // The generator is the same on every platform, the distributions of the standard library are not: the numbers are
  // taken from it directly.
  std::mt19937 random(7); // NOLINT(bugprone-random-generator-seed): the test repeats its inputs
  constexpr uint32_t max_fragments = 25;
  for (int text_index = 0; text_index < 3000; ++text_index) {
    std::string text;
    for (auto count = random() % max_fragments; count > 0; --count) {
      text += fragments[random() % fragments.size()];
    }
    for (const auto& sentence : Normalizer().Normalize(text)) {
      INFO(text << " -> " << sentence.text);
      CHECK(InAlphabet(sentence.text));
    }
  }
}
