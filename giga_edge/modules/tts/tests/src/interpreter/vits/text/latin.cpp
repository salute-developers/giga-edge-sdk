#include "giga_edge/modules/tts/interpreter/backends/vits/text/latin.h"

#include "giga_edge/lib/data_reader/data_reader.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;
using namespace giga_edge::vits;

namespace {

const WordReader& Reader() {
  static const WordReader reader(*CreateDataReader("data/vits"), "text/ru");
  return reader;
}

} // namespace

TEST_CASE("vits latin: transliteration by rules", "[tts][vits][latin]") {
  CHECK(Transliterate("coldplay") == "колдплэй");
  CHECK(Transliterate("Queen") == "квин");
  CHECK(Transliterate("night") == "найт");
  CHECK(Transliterate("yellow") == "йеллоу");
  CHECK(Transliterate("city") == "сити");
  CHECK(Transliterate("imagine") == "имагин");
  CHECK(Transliterate("eleven") == "элевен");
  CHECK(Transliterate("player") == "плэйэр");
  CHECK(Transliterate("fox") == "фокс");
  CHECK(Transliterate("jet") == "джет");
  CHECK(Transliterate("").empty());
}

TEST_CASE("vits latin: letters", "[tts][vits][latin]") {
  CHECK(SpellLatin("USB") == "ю-эс-би");
  CHECK(SpellLatin("www") == "дабл-ю-дабл-ю-дабл-ю");
  CHECK(SpellLatin("xyz") == "икс-уай-зэд");
  CHECK(SpellCyrillic("МЧС") == "эм-чэ-эс");
  CHECK(SpellCyrillic("ЖКХ") == "жэ-ка-ха");
  CHECK(SpellCyrillic("ЦСКА") == "цэ-эс-ка-а");
  CHECK(SpellCyrillic("ЪЁЛ") == "ё-эль");
}

TEST_CASE("vits latin: the table, letters or the rules", "[tts][vits][latin]") {
  const auto& reader = Reader();
  CHECK(reader.ReadLatin("Bluetooth") == "блют+ус");
  CHECK(reader.ReadLatin("WI-FI") == "вай-ф+ай");
  CHECK(reader.ReadLatin("GPT") == "джи-пи-т+и");
  CHECK(reader.ReadLatin("PDF") == "пэ-дэ-+эф");
  CHECK(reader.ReadLatin("kbd") == "кей-би-ди");
  CHECK(reader.ReadLatin("HDMI") == "эйч-ди-эм-+ай");
  CHECK(reader.ReadLatin("NASA") == "наса");
  CHECK(reader.ReadLatin("PHARAOH") == "фара+он");
  CHECK(reader.ReadLatin("ZAPP") == "зэд-эй-пи-пи");
  CHECK(reader.ReadLatin("ZAPPA") == "заппа");
  CHECK(reader.ReadLatin("x") == "икс");
  CHECK(reader.ReadLatin("html") == "эйч-ти-эм-эл");
  CHECK(reader.ReadLatin("Tellyve") == "теллив");
  CHECK(reader.ReadAbbreviation("МЧС") == "эм-чэ-эс");
  CHECK(reader.ReadAbbreviation("МГУ") == "эм-гэ-у");
  CHECK(reader.ReadAbbreviation("ТВ") == "тэ-вэ");
  CHECK(reader.ReadAbbreviation("ООН") == "оон");
  CHECK(reader.ReadAbbreviation("ГОСТ") == "гост");
  CHECK(reader.ReadAbbreviation("США") == "сэ-шэ-+а");
  CHECK(reader.ReadAbbreviation("МКАД") == "мк+ад");
  CHECK(reader.FromTable("неттакого").empty());

  // A reading of words given from outside.
  const WordReader english(*CreateDataReader("data/vits"), "text/ru", [](std::string_view word) {
    return "[" + std::string(word) + "]";
  });
  CHECK(english.ReadLatin("Believer") == "бил+ивэр");
  CHECK(english.ReadLatin("Sunshine") == "[Sunshine]");
  CHECK(english.ReadLatin("USB") == "ю-эс-б+и");
  // It gets a word with diacritics as written, the rules and the table read its plain letters.
  CHECK(english.ReadLatin("Hëlvik") == "[Hëlvik]");
  CHECK(english.ReadLatin("Beyoncé") == "бей+онсе");
  CHECK(reader.ReadLatin("Hëlvik") == Transliterate("Helvik"));
}
