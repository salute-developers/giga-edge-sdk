#include "giga_edge/modules/tts/interpreter/backends/vits/text/english.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;
using Catch::Matchers::Contains;

namespace {

// The test file is built from english_words.txt, a sample of CMUdict.
constexpr char kDictionary[] = "vits/text/ru/english.dict";
constexpr char kSource[] = "vits/text/ru/english_words.txt";

const EnglishReader& PackageReader() {
  static const auto reader = CreateDataReader("data/");
  static const EnglishReader english(*reader, kDictionary);
  return english;
}

uint32_t Adler32(const std::string& data) {
  uint32_t low = 1;
  uint32_t high = 0;
  for (const char symbol : data) {
    low = (low + static_cast<unsigned char>(symbol)) % 65521;
    high = (high + low) % 65521;
  }
  return (high << 16) | low;
}

// The file with its checksum line.
std::string WithChecksum(const std::string& body) {
  std::ostringstream line;
  line << "#adler32 " << std::hex << std::setw(8) << std::setfill('0') << Adler32(body) << '\n';
  return body + line.str();
}

IDataSpanPtr MakeSpan(const std::string& text) {
  return Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end()));
}

// A reader of a file given as text as it is.
EnglishReader MakeRawReader(const std::string& text, bool verify = true) {
  return EnglishReader(MakeSpan(text), "test.dict", verify);
}

// A reader of a file given as text without its checksum line.
EnglishReader MakeReader(const std::string& body, bool verify = true) {
  return MakeRawReader(WithChecksum(body), verify);
}

size_t CountOf(const std::string& text, char symbol) {
  size_t count = 0;
  for (const char c : text) {
    count += c == symbol;
  }
  return count;
}

std::string Repeat(const std::string& text, size_t times) {
  std::string result;
  for (size_t i = 0; i < times; ++i) {
    result += text;
  }
  return result;
}

} // namespace

TEST_CASE("vits English reader writes dictionary words in Russian letters", "[tts][vits][english]") {
  struct Case {
    const char* word;
    const char* russian;
  };
  const auto& english = PackageReader();
  for (const auto& [word, russian] : std::vector<Case>{
         {"queen", "кв+ин"},          // 'w' after a consonant
         {"weekend", "уик+энд"},      // 'w' before a vowel; a compound is stressed on its last part
         {"bluetooth", "блют+ус"},    // th -> с, 'l' before 'u' -> "лю", the compound stress
         {"online", "онл+айн"},       // the compound stress
         {"telegram", "тэлэгр+ам"},   // -gram keeps "а" under the adapted stress
         {"blinding", "бл+айндинг"},  // ng
         {"think", "с+инк"},          // ng before k
         {"finger", "ф+ингэр"},       // ng before g, an unstressed "er"
         {"lights", "л+айтс"},        //
         {"the", "з+э"},              // th -> з, a word without a stressed vowel
         {"a", "+а"},                 //
         {"apple", "+эпл"},           // a schwa without a vowel letter is dropped
         {"google", "г+угл"},         //
         {"example", "игз+эмпл"},     //
         {"hello", "хэл+оу"},         // a schwa tied to a consonant takes the silent vowel letter next to it
         {"dragons", "др+эгонз"},     // a schwa by its letter 'o'
         {"success", "сакс+эс"},      // a schwa by its letter 'u'
         {"functional", "ф+анкшнал"}, // a schwa between "sh" and "n" is dropped
         {"world", "у+орлд"},         // "or" after 'w'
         {"first", "ф+ёрст"},         // a stressed "er" after a consonant
         {"cold", "к+олд"},           // "о" before a consonant
         {"info", "+инфо"},           // an unstressed final 'o'
         {"auto", "+ото"},            //
         {"yellow", "+елоу"},         // 'y' before a vowel at the start, "ow"
         {"music", "мь+юзик"},        // 'y' after a consonant
         {"user", "+юзэр"},           //
         {"william", "у+ильям"},      // 'y' between a consonant and a vowel
         {"yahoo", "+яху"},           // no compound stress on the last sound
         {"wood", "в+уд"},            // 'w' before "u"
         {"docs", "д+окс"},           // "aa" spelled 'o'
         {"null", "н+ал"},            //
         {"page", "п+эйдж"},          // j -> дж, a hard consonant before "э"
         {"don't", "д+онт"},          // an apostrophe
         {"hmm", "хм"},               // no vowels: no stress mark
         {"radio", "р+эйдио"},        // a final 'o' without the Russian stress; no compound stress on the last sound
         {"radios", "р+эйдиоз"},      // ... nor before a final 's'
         {"yesterday", "+естэрдэй"},  // ... nor on "-day"
         {"celebrate", "с+элэбрэйт"}, // ... nor on "-ate"
         {"website", "у+эбсайт"},     // "site" takes no stress in Russian
         {"laptop", "л+эптоп"},       //
         {"chrome", "кр+ом"},         // "о" before a consonant
         {"lose", "л+уз"},            // 'l' + "у" is "лю" after a consonant only
         {"luna", "л+уна"},           //
         {"glue", "гл+у"},            //
         {"ipad", "айп+эд"},          // 'a' under the compound stress is "э"
       }) {
    CAPTURE(word);
    REQUIRE(english.Contains(word));
    REQUIRE(english.Read(word) == russian);
  }
}

TEST_CASE("vits English reader takes any case", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const char* word : {"Weekend", "WEEKEND", "weekEND", "wEEKEND"}) {
    CAPTURE(word);
    REQUIRE(english.Read(word) == "уик+энд");
    REQUIRE(english.Transcribe(word) == "W IY1 K EH2 N D");
    REQUIRE(english.Contains(word));
  }
  REQUIRE(english.Read("DON'T") == "д+онт");
}

TEST_CASE("vits English reader answers nothing to what is not a Latin word", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const std::string& word : std::vector<std::string>{
         "",
         "'",
         "''",
         "wee kend",
         " queen",
         "queen ",
         "queen.",
         "queen1",
         "вика",
         "\xFF",
         std::string("que\0en", 6),
         "[q]",
         "q\t"
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word).empty());
    REQUIRE(english.Transcribe(word).empty());
    REQUIRE_FALSE(english.Contains(word));
  }
}

TEST_CASE("vits English reader spells out unknown words by the rules", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const char* word : {"weeknd", "coldplay", "miyagi", "xiaomi", "sss", "qu", "'s", "aaaaaaaaaaaaaaaaaaaaaaaa"}) {
    CAPTURE(word);
    REQUIRE_FALSE(english.Contains(word));
    const auto russian = english.Read(word);
    REQUIRE_FALSE(russian.empty());
    REQUIRE(CountOf(russian, '+') <= 1);
    REQUIRE_FALSE(english.Transcribe(word).empty());
  }
  // The phoneme model silences q; practical transcription still writes it.
  REQUIRE(english.Read("q") == "к");
  REQUIRE(english.Transcribe("q").empty());
  // A word of letters with vowels has one stress.
  REQUIRE(CountOf(english.Read("coldplay"), '+') == 1);
  REQUIRE(CountOf(english.Read("aaaaaaaaaaaaaaaaaaaaaaaa"), '+') == 1);
}

TEST_CASE("vits English reader reads words of other languages as they are written", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const auto& [word, russian] : std::vector<std::pair<const char*, const char*>>{
         {"Rammstein", "раммшт+айн"},
         {"Rakhim", "рахим"},
         {"Miyagi", "ми+яги"},
         {"Murovei", "муровей"},
         {"Xiaomi", "сяом+и"},
       }) {
    CAPTURE(word);
    REQUIRE_FALSE(english.Contains(word));
    REQUIRE(english.Read(word) == russian);
    REQUIRE_FALSE(english.Transcribe(word).empty()); // the phonemes stay English
  }
  // A dictionary word is read by the dictionary whatever it looks like, but a Russian name with sure signs is not.
  REQUIRE(english.Read("william") == "у+ильям");
  REQUIRE(english.Contains("smirnov"));
  REQUIRE(english.Read("smirnov") == "смирнов");
  REQUIRE(english.Transcribe("smirnov") == "S M ER1 N AA0 F");
}

TEST_CASE("vits English reader reads words up to 40 letters", "[tts][vits][english]") {
  const auto& english = PackageReader();
  const auto longest = Repeat("bluetooth", 4) + "blue";
  REQUIRE(longest.size() == EnglishReader::kMaxLetters);
  REQUIRE(CountOf(english.Read(longest), '+') == 1);
  REQUIRE_FALSE(english.Transcribe(longest).empty());
  for (const auto& word : {longest + "s", Repeat("bluetooth", 1000), Repeat("smirnov", 6)}) {
    REQUIRE(english.Read(word).empty());
    REQUIRE(english.Transcribe(word).empty());
    REQUIRE_FALSE(english.Contains(word));
  }
}

TEST_CASE("vits English reader finds every word of its source list", "[tts][vits][english]") {
  const auto reader = CreateDataReader("data/");
  const auto source = ToString(reader->ReadTransientData(kSource));
  const auto& english = PackageReader();
  size_t words = 0;
  for (size_t pos = 0; pos < source.size();) {
    auto end = source.find('\n', pos);
    end = end == std::string::npos ? source.size() : end;
    const auto line = source.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto space = line.find(' ');
    const auto word = line.substr(0, space);
    CAPTURE(word);
    REQUIRE(english.Contains(word));
    REQUIRE(english.Transcribe(word) == line.substr(space + 1));
    REQUIRE(CountOf(english.Read(word), '+') == (word == "hmm" || IsSurelyForeign(word) ? 0U : 1U));
    ++words;
  }
  REQUIRE(words == 68);
}

TEST_CASE("vits English reader takes the widest rule window", "[tts][vits][english]") {
  // Letters: 'b' -> B; 'c' -> K, S before 'e', CH at the start before 'h'; 'h' silent after 'c'; 'e' -> EH1, silent
  // at the end; 'a' -> AE1, unstressed at the end after 'b'.
  const auto english = MakeReader(
    "#vits english 7 windows 11 01 00 stress 0\n"
    "[a]\tAE1\n"
    "[b]\tB\n"
    "[c]\tK\n"
    "[c]e\tS\n"
    "[e]\tEH1\n"
    "[e]$\t_\n"
    "[h]\tHH\n"
    "^[c]h\tCH\n"
    "b[a]$\tAH0\n"
    "c[h]$\t_\n"
    "c[h]a\t_\n"
  );
  REQUIRE(english.Transcribe("cab") == "K AE1 B");
  REQUIRE(english.Transcribe("ceb") == "S EH1 B");
  REQUIRE(english.Transcribe("chab") == "CH AE1 B");
  REQUIRE(english.Transcribe("bach") == "B AE1 K");
  REQUIRE(english.Transcribe("bace") == "B AE1 S");
  REQUIRE(english.Transcribe("ba") == "B AH0");
  REQUIRE(english.Read("ba") == "б+а");
  REQUIRE(english.Transcribe("bab") == "B AE1 B");
  // A letter without a rule is silent.
  REQUIRE(english.Transcribe("bxb") == "B B");
  REQUIRE(english.Read("bxb") == "бб");
  REQUIRE_FALSE(english.Contains("cab"));
}

TEST_CASE("vits English reader takes the windows of its header", "[tts][vits][english]") {
  const std::string rules = "[a]\tAE1\n[b]\tB\n[c]\tK\n^[c]h\tCH\nc[h]a\t_\n";
  REQUIRE(MakeReader("#vits english 7 windows 11 01 00 stress 0\n" + rules).Transcribe("chab") == "CH AE1 B");
  REQUIRE(MakeReader("#vits english 7 windows 01 00 stress 0\n" + rules).Transcribe("chab") == "K AE1 B");
  REQUIRE(MakeReader("#vits english 7 windows 00 stress 0\n" + rules).Transcribe("chab") == "K AE1 B");
}

TEST_CASE("vits English reader stresses a word of the rules by its ending", "[tts][vits][english]") {
  // Without the check of the file at load: it would reject "x".
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 2\n"
    "1:\t0\n"    // one vowel: the last one
    "2:\t1\n"    // two vowels: the first one
    "2:ab\t0\n"  // ... but the last one before "ab"
    "2:bb\t5\n"  // a vowel the word has not: nothing changes
    "2:bc\tx\n"  // not a number: nothing changes
    "3:cab\t0\n" // longer than the longest ending of the header: not looked up
    "[a]\tAE0\n"
    "[b]\tB\n"
    "[c]\tK\n"
    "ab\tAE0 B\n" // a dictionary word keeps its stress
    ,
    false
  );
  REQUIRE(english.Transcribe("ba") == "B AE1");
  REQUIRE(english.Transcribe("baba") == "B AE1 B AE0");
  REQUIRE(english.Transcribe("abab") == "AE0 B AE1 B");
  REQUIRE(english.Read("abab") == "аб+эб");
  REQUIRE(english.Transcribe("ababb") == "AE0 B AE0 B B");
  REQUIRE(english.Transcribe("ababc") == "AE0 B AE0 B K");
  REQUIRE(english.Transcribe("acab") == "AE0 K AE1 B");
  REQUIRE(english.Transcribe("aacab") == "AE0 AE0 K AE0 B");
  REQUIRE(english.Transcribe("ab") == "AE0 B");
  REQUIRE(english.Transcribe("bbb") == "B B B");
  // A bad answer leaves the stress of the letter rules as it is.
  const auto bad = MakeReader("#vits english 7 windows 00 stress 2\n2:be\t5\n[a]\tAE2\n[b]\tB\n[e]\tEH1\n", false);
  REQUIRE(bad.Transcribe("abe") == "AE2 B EH1");
  // Seven vowels and more share the rules of seven.
  const auto many = MakeReader("#vits english 7 windows 00 stress 0\n7:\t2\n[a]\tAE0\n");
  REQUIRE(many.Transcribe("aaaaaaaaa") == "AE0 AE0 AE0 AE0 AE0 AE0 AE1 AE0 AE0");
}

TEST_CASE("vits English reader falls back to the rules on a damaged dictionary entry", "[tts][vits][english]") {
  // Without the check of the file at load.
  const auto english = MakeReader(
    "#vits english 7 windows 11 01 00 stress 0\n"
    "[a]\tAE1\n"
    "[b]\tB\n"
    "aab\tAA1 AA1 B1\n" // a stress on a consonant
    "ab\tAA1 B.B.B\n"   // three phonemes from a letter are written as they are
    "aba\tAA1 B\n"      // fewer chunks than letters
    "abb\tAA1 B _ _\n"  // more chunks than letters
    "bab\tB XX1 B\n"    // not a phoneme
    "bba\tB _ AA3\n"    // a wrong stress
    ,
    false
  );
  REQUIRE(english.Transcribe("ab") == "AA1 B B B");
  REQUIRE(english.Transcribe("aba") == "AE1 B AE2");
  REQUIRE(english.Transcribe("abb") == "AE1 B B");
  REQUIRE(english.Transcribe("bab") == "B AE1 B");
  REQUIRE(english.Transcribe("aab") == "AE1 AE2 B");
  REQUIRE(english.Transcribe("bba") == "B B AE1");
  // A damaged rule rejects the entire transcription.
  const auto damaged_rule = MakeReader("#vits english 7 windows 11 01 00 stress 0\n[a]\tAE1.\n[b]\tB\n", false);
  REQUIRE(damaged_rule.Transcribe("ab").empty());
}

TEST_CASE("vits English reader keeps one stress", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 11 01 00 stress 0\n"
    "ab\tAE2 B\n"            // only a secondary stress: it becomes the primary one
    "aba\tAE1 B AE1\n"       // two primary ones: the first stays
    "abab\tAE0 B AE1 B\n"    // English stress
    "abba\tAE1 B _ AE2\n"    // a secondary stress on the last vowel, but no stressed part: English stress
    "bab\tB AE0 B\n"         // no stress: the first written vowel
    "baba\tB AE2 B AE1\n"    // a secondary stress before the primary one changes nothing
    "bxbab\tB AH0 B AE0 B\n" // the first vowel has no letter: the next one
  );
  REQUIRE(english.Transcribe("ab") == "AE1 B");
  REQUIRE(english.Read("ab") == "+эб");
  REQUIRE(english.Transcribe("aba") == "AE1 B AE2");
  REQUIRE(english.Read("aba") == "+эба");
  REQUIRE(english.Read("abab") == "аб+эб");
  REQUIRE(english.Read("abba") == "+эба");
  REQUIRE(english.Read("baba") == "баб+э");
  REQUIRE(english.Read("bab") == "б+эб");
  REQUIRE(english.Read("bxbab") == "бб+эб");
}

TEST_CASE("vits English reader writes every phoneme", "[tts][vits][english]") {
  struct Case {
    const char* chunks;
    const char* russian;
  };
  // One word "q..." of as many letters as chunks; the dictionary entries are made up for the phonemes.
  for (const auto& [chunks, russian] : std::vector<Case>{
         {"B AA1", "б+а"},       {"B AE0", "б+э"},       {"B AH1", "б+а"},         {"B AO1", "б+о"},
         {"B AW1", "б+ау"},      {"B AY1", "б+ай"},      {"B EH1", "б+э"},         {"B ER0", "б+эр"},
         {"B EY1", "б+эй"},      {"B IH1", "б+и"},       {"B IY1", "б+и"},         {"B OW1", "б+оу"},
         {"B OY1", "б+ой"},      {"B UH1", "б+у"},       {"B UW1", "б+у"},         {"CH AA1", "ч+а"},
         {"D AA1", "д+а"},       {"DH AA1", "з+а"},      {"F AA1", "ф+а"},         {"G AA1", "г+а"},
         {"HH AA1", "х+а"},      {"JH AA1", "дж+а"},     {"K AA1", "к+а"},         {"L AA1", "л+а"},
         {"M AA1", "м+а"},       {"N AA1", "н+а"},       {"NG AA1", "нг+а"},       {"P AA1", "п+а"},
         {"R AA1", "р+а"},       {"S AA1", "с+а"},       {"SH AA1", "ш+а"},        {"T AA1", "т+а"},
         {"TH AA1", "с+а"},      {"V AA1", "в+а"},       {"W AA1", "у+а"},         {"Y AA1", "+я"},
         {"Z AA1", "з+а"},       {"ZH AA1", "ж+а"},      {"AA1 W", "+ав"},         {"AA1 Y", "+ай"},
         {"B Y UW1", "бь+ю"},    {"Y EH1", "+е"},        {"Y AO1", "+ё"},          {"B Y AO0 B AA1", "бьоб+а"},
         {"Y IH1", "й+и"},       {"B Y IH1", "бь+и"},    {"L UW1", "л+у"},         {"L UH1", "л+у"},
         {"B L UW1", "бл+ю"},    {"B AA1 Y B", "б+айб"}, {"B OY1 Y AA0", "б+ойа"}, {"B AA1 Y AA0", "б+ая"},
         {"B OW1 AA0", "б+оуа"}, {"B OW1 B", "б+об"},    {"Y ER1", "+ер"},         {"B ER1", "б+ёр"},
         {"AA1 ER0", "+аэр"},
       }) {
    std::string word = "q";
    for (const char symbol : std::string(chunks)) {
      word += symbol == ' ' ? "q" : "";
    }
    const auto english = MakeReader("#vits english 7 windows 11 01 00 stress 0\n" + word + "\t" + chunks + "\n");
    CAPTURE(chunks);
    REQUIRE(english.Contains(word));
    REQUIRE(english.Read(word) == russian);
  }
}

TEST_CASE("vits English reader spells vowels by their letters", "[tts][vits][english]") {
  struct Case {
    const char* entry;
    const char* russian;
  };
  for (const auto& [entry, russian] : std::vector<Case>{
         {"bab\tB AH0 B", "б+аб"},
         {"bob\tB AH0 B", "б+об"},
         {"bub\tB AH0 B", "б+аб"},
         {"beb\tB AH0 B", "б+эб"},
         {"bib\tB AH0 B", "б+иб"},
         {"byb\tB AH0 B", "б+иб"},
         {"bb\tB AH0.B", "бб"},
         {"bab\tB AH1 B", "б+аб"},
         {"bob\tB AH1 B", "б+аб"},
         {"bob\tB AA1 B", "б+об"},
         {"bab\tB AA1 B", "б+аб"},
         {"bab\tB AE1 B", "б+эб"},
         {"bab\tB AE2 B", "б+эб"},
         {"baba\tB AE1 B AE0", "б+эба"},
         {"bob\tB ER0 B", "б+орб"},
         {"bab\tB ER0 B", "б+арб"},
         {"bub\tB ER0 B", "б+эрб"},
         {"wob\tW ER1 B", "у+орб"},
         {"wib\tW ER1 B", "у+эрб"},
         {"bob\tB ER1 B", "б+ёрб"},
         {"ob\tER1 B", "+эрб"},
         {"bold\tB OW1 L D", "б+олд"},
         {"bol\tB OW1 L", "б+ол"},
         {"bo\tB OW1", "б+оу"},
         {"bao\tB AH1 OW0", "б+ао"},
         {"baow\tB AA1 OW0 _", "б+аоу"},
         {"bau\tB AA1 OW0", "б+аоу"},
         {"heb\tHH.AH0 _ B", "х+эб"},
         {"ehb\tAA1 HH.AH0 B", "+ахб"},
         {"hbe\tHH B.AH0 _", "хб+э"},
         {"hbeb\tHH B.AH0 B AA1", "хбб+а"},
         {"bion\tB SH AH0 N", "бшн"},
         {"bion\tB ZH AH0 N", "бжн"},
         {"bion\tB CH AH0 N", "бч+он"},
         {"bion\tB SH AH0 M", "бш+ом"},
         {"bion\tB SH AH1 N", "бш+ан"},
       }) {
    const auto english = MakeReader(std::string("#vits english 7 windows 11 01 00 stress 0\n") + entry + "\n");
    CAPTURE(entry);
    const std::string text(entry);
    REQUIRE(english.Read(text.substr(0, text.find('\t'))) == russian);
  }
}

TEST_CASE("vits English reader may be read from many threads", "[tts][vits][english]") {
  const auto& english = PackageReader();
  const std::vector<std::string> words = {"queen", "weekend", "coldplay", "bluetooth", "miyagi", "the"};
  std::vector<std::string> expected;
  expected.reserve(words.size());
  for (const auto& word : words) {
    expected.push_back(english.Read(word));
  }
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      for (int round = 0; round < 500; ++round) {
        for (size_t i = 0; i < words.size(); ++i) {
          wrong += english.Read(words[i]) != expected[i];
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  REQUIRE(wrong == 0);
}

TEST_CASE("vits English reader rejects a bad file", "[tts][vits][english]") {
  const auto reader = CreateDataReader("data/");
  REQUIRE_THROWS_AS(EnglishReader(*reader, "vits/text/ru/no_such.dict"), std::runtime_error);
  const auto no_data = CreateCachingDataReader(nullptr);
  REQUIRE_THROWS_WITH(EnglishReader(*no_data, "a.dict"), Contains("a.dict") && Contains("no data"));

  struct Case {
    std::string text;
    const char* error;
  };
  const std::string header = "#vits english 7 windows 00 stress 0\n";
  // As they are, without a checksum line added: whatever `verify` is.
  for (const auto verify : {true, false}) {
    for (const auto& [text, error] : std::vector<Case>{
           {"", "not an English pronunciation file"},
           {"#vits english 2 windows 00 stress 0\n#adler32 00000000\n", "not an English pronunciation file"},
           {"[a]\tAE1\n", "not an English pronunciation file"},
           {"#vits english 7 windows 00 stress 0", "truncated"},
           {header + "[a]\tAE1\n", "truncated"},
           {header + "[a]\tAE1\n#adler32 0000000\n", "truncated"},
           {header + "[a]\tAE1\n#adler32 0000000g\n", "truncated"},
           {header + "[a]\tAE1\n#adler32 00000000", "truncated"},
           {header + "[a]\tAE1\n#adler33 00000000\n", "truncated"},
         }) {
      CAPTURE(text, verify);
      REQUIRE_THROWS_WITH(MakeRawReader(text, verify), Contains("test.dict") && Contains(error));
    }
  }
  // A wrong checksum: found by the check only.
  const auto wrong = header + "[a]\tAE1\n#adler32 00000000\n";
  REQUIRE_THROWS_WITH(MakeRawReader(wrong), Contains("wrong checksum"));
  REQUIRE(MakeRawReader(wrong, false).Transcribe("a") == "AE1");
  REQUIRE(MakeRawReader(WithChecksum(header + "[a]\tAE1\n")).Transcribe("a") == "AE1");

  // With a right checksum line.
  for (const auto& [text, error] : std::vector<Case>{
         {"#vits english 7 windows stress 0\n", "a bad header"},
         {"#vits english 7 windows 11 22 00 stress 0\n", "a bad header"},
         {"#vits english 7 windows 11 02 00 stress 0\n", "a bad header"},
         {"#vits english 7 windows 11 01 00\n", "a bad header"},
         {"#vits english 7 windows 11 01 00 stress 10\n", "a bad header"},
         {"#vits english 7 windows 11 01 00 stress x\n", "a bad header"},
         {"#vits english 7 windows 1 01 00 stress 0\n", "a bad header"},
         {"#vits english 7 windows 111 00 stress 0\n", "a bad header"},
         {"#vits english 7 windows 11 01 00 stress 0 \n", "a bad header"},
         {"#vits english 7 windows  00 stress 0\n", "a bad header"},
         {header + "[b]\tB\n[a]\tAE1\n", "line 3 is out of order"},
         {header + "[a]\tAE1\n[a]\tAE1\n", "line 3 is out of order"},
         {header + "[a]\tAE1\n[b] B\n", "line 3 has no key"},
         {header + "\tAE1\n", "line 2 has no key"},
         {header + "\n", "line 2 has no key"},
         {header + "[a]\tAE9\n", "line 2 is damaged"},
         {header + "[a\tAE1\n", "line 2 is damaged"},
         {header + "[a]\tAE1 B\n", "line 2 is damaged"},
         {header + "2:\tx\n", "line 2 is damaged"},
         {header + "2:\t12\n", "line 2 is damaged"},
         {header + ">\t1\n", "line 2 is damaged"},
         {header + ">b\t2\n", "line 2 is damaged"},
         {header + "a-b\tAE1 _ B\n", "line 2 is damaged"},
         {header + "ab\tAE1\n", "line 2 is damaged"},
         {header + "ab\tAE1 B _\n", "line 2 is damaged"},
         {header + "ab\t\n", "line 2 is damaged"},
         {header + "ab\tAE1 XX\n", "line 2 is damaged"},
       }) {
    CAPTURE(text);
    REQUIRE_THROWS_WITH(MakeReader(text), Contains("test.dict") && Contains(error));
  }
  // Skipping validation at load still rejects a damaged rule when it is read.
  REQUIRE(MakeReader(header + "[a]\tAE9\n[b]\tB\n", false).Transcribe("ab").empty());
  // Every kind of entry passes the check.
  REQUIRE_NOTHROW(MakeReader(header + "'\tAH1\n1:\t0\n>b'\t1\n[a]\tAE1.B\n^[b]$\t_\nab\tAE1 B\n"));
  // Apostrophes alone are not a word, even where the file has an entry for them.
  const auto apostrophe = MakeReader("#vits english 7 windows 11 01 00 stress 0\n'\tAH1\n[']\tAH1\n");
  REQUIRE(apostrophe.Read("'").empty());
  REQUIRE(apostrophe.Transcribe("'").empty());
  REQUIRE_FALSE(apostrophe.Contains("'"));
  REQUIRE(apostrophe.Read("''").empty());
  // Only the header and the checksum: every word is silent.
  const auto empty = MakeReader("#vits english 7 windows 11 01 00 stress 0\n");
  REQUIRE(empty.Read("queen").empty());
  REQUIRE(empty.Transcribe("queen").empty());
  REQUIRE_FALSE(empty.Contains("queen"));
}

TEST_CASE("vits English reader notices a damaged or a cut test dictionary", "[tts][vits][english]") {
  const auto reader = CreateDataReader("data/");
  const auto text = ToString(reader->ReadTransientData(kDictionary));
  REQUIRE(EnglishReader(MakeSpan(text), "english.dict").Read("queen") == "кв+ин");
  REQUIRE(EnglishReader(MakeSpan(text), "english.dict", false).Read("queen") == "кв+ин");

  // A letter changed in the middle: the check finds it; without the check the file loads.
  auto damaged = text;
  const auto queen = damaged.find("\nqueen\t");
  REQUIRE(queen != std::string::npos);
  damaged[queen + 1] = 'p';
  REQUIRE_THROWS_WITH(EnglishReader(MakeSpan(damaged), "english.dict"), Contains("wrong checksum"));
  REQUIRE_NOTHROW(EnglishReader(MakeSpan(damaged), "english.dict", false));

  // Cut at a line end, in a line, without the checksum line: truncated whatever the check is.
  const auto middle = text.find('\n', text.size() / 2) + 1;
  const auto last_line = text.rfind('\n', text.size() - 2) + 1;
  for (const auto& cut : {
         text.substr(0, middle), text.substr(0, middle + 3), text.substr(0, last_line), text.substr(0, text.size() - 1)
       }) {
    for (const auto verify : {true, false}) {
      CAPTURE(cut.size(), verify);
      REQUIRE_THROWS_WITH(EnglishReader(MakeSpan(cut), "english.dict", verify), Contains("truncated"));
    }
  }
}

TEST_CASE("vits English reader opens a file only if the package has it", "[tts][vits][english]") {
  const auto reader = CreateDataReader("data/");
  REQUIRE(EnglishReader::Open(*reader, "vits/text/ru/no_such.dict") == nullptr);
  for (const auto verify : {true, false}) {
    const auto english = EnglishReader::Open(*reader, kDictionary, verify);
    REQUIRE(english != nullptr);
    REQUIRE(english->Read("queen") == "кв+ин");
  }
  const auto damaged = CreateAnyPathSameSpanDataReader(MakeSpan("#vits english 7 windows 00 stress 0\n"));
  REQUIRE_THROWS_WITH(EnglishReader::Open(*damaged, "a.dict"), Contains("truncated"));
}

TEST_CASE("vits English reader stresses a compound on its last part only by the list", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    ">date\t1\n"                // a part in -ate: never stressed
    ">kabod\t1\n"               // a whole word
    ">po\t1\n"                  // a part that ends the word with a vowel: never stressed
    ">pod\t1\n"                 // a part after a dictionary word
    ">poz\t1\n"                 // a part that ends the word with 'z' only after its vowel
    "ga\tG AA1\n"               //
    "gapod\tG AA1 P AA0 D\n"    // -> the last vowel is not stressed in English
    "ka\tK AA1\n"               //
    "kabod\tK AA1 B AA2 D\n"    // -> the whole word is on the list
    "kadate\tK AA1 D EY2 T _\n" // -> "-ate"
    "kapad\tK AA1 P AA2 D\n"    // -> "pad" is not on the list
    "kapo\tK AA1 P AA2\n"       // -> the last sound
    "kapod\tK AA1 P AA2 D\n"    // -> "ka" + "pod"
    "kapodp\tK AA1 P AA2 D P\n" // -> "pod" does not end the word
    "kapods\tK AA1 P AA2 D Z\n" // -> "ka" + "pod" + 's'
    "kapos\tK AA1 P AA2 S\n"    // -> a final 's' only after the vowel
    "kapot\tK AA1 P AA0 T\n"    // -> no secondary stress
    "kapoz\tK AA1 P AA2 Z\n"    // -> ... or 'z'
    "xapod\tK AA1 P AA2 D\n"    // -> "xa" is not a word of the dictionary
  );
  for (const auto& [word, russian] : std::vector<std::pair<const char*, const char*>>{
         {"kabod", "каб+од"},
         {"kadate", "к+адэйт"},
         {"kapad", "к+апад"},
         {"kapo", "к+апо"},
         {"kapod", "кап+од"},
         {"kapodp", "к+аподп"},
         {"kapods", "кап+одз"},
         {"kapos", "к+апос"},
         {"kapoz", "к+апоз"},
         {"gapod", "г+апод"},
         {"kapot", "к+апот"},
         {"xapod", "к+апод"},
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word) == russian);
  }
}

TEST_CASE("vits English reader takes a stress rule only for a vowel of the word", "[tts][vits][english]") {
  // Two vowels: "2" is out of the word and changes nothing.
  const auto english = MakeReader("#vits english 7 windows 00 stress 2\n2:be\t2\n[a]\tAE2\n[b]\tB\n[e]\tEH1\n", false);
  REQUIRE(english.Transcribe("abe") == "AE2 B EH1");
  const auto last = MakeReader("#vits english 7 windows 00 stress 2\n2:be\t1\n[a]\tAE2\n[b]\tB\n[e]\tEH1\n");
  REQUIRE(last.Transcribe("abe") == "AE1 B EH2");
}

TEST_CASE("vits English reader rejects entries outside the format grammar", "[tts][vits][english]") {
  for (const std::string entry :
       {"[1]\tAE1",
        "a\tAE",
        "b\tB0",
        "b\tB2",
        "0:anything!\t0",
        "8:a\t0",
        "2:ab!\t0",
        "2:abcde\t0",
        "2:ab\t2",
        "a^[b]\tB",
        "[b]$a\tB",
        "[b][c]\tB",
        "[b]$$$$$$$$$$\tB"}) {
    CAPTURE(entry);
    REQUIRE_THROWS_WITH(MakeReader("#vits english 7 windows 00 stress 4\n" + entry + "\n"), Contains("damaged"));
  }
}

TEST_CASE("vits English reader keeps chosen spellings and compound exceptions", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"soul", "с+оул"},
         {"shows", "ш+оуз"},
         {"Telegram", "тэлэгр+ам"},
         {"saxophone", "саксоф+он"},
         {"instagram", "инстагр+ам"},
         {"hologram", "хологр+ам"},
         {"Fireball", "ф+айэрбол"},
         {"Snowball", "сн+оубол"},
         {"textbook", "т+экстбук"},
         {"headphones", "х+эдфонз"},
         {"improv", "+импров"},
         {"zhanna", "жанна"},
         {"zhenya", "женя"},
         {"medvedev", "медведев"},
         {"nabokov", "набоков"},
         {"zhukov", "жуков"}
       }) {
    CAPTURE(word);
    REQUIRE(english.Contains(word));
    REQUIRE(english.Read(word) == expected);
  }
}

TEST_CASE("vits English names use contextual package rules and explicit mode", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "!er\t1\n"
    "!ser\t0\n"
    "@[a]\tа\n"
    "@[b]\tб\n"
    "@[ch]\tч\n"
    "@[d]\tд\n"
    "@[e]\tе\n"
    "@[e]$\t_\n"
    "@[o]\tё\n"
    "@[ough]\tо\n"
    "@[r]\tр\n"
    "@[s]\tс\n"
    "@[t]\tт\n"
    "@[tch]\tч\n"
    "@[u]\tу\n"
    "@[y]\tи\n"
    "@^[e]d\tэ\n"
    "@a[ch]$\tх\n"
    "@a[s]a\tз\n"
    "babe\tB EY1 B _\n"
  );
  REQUIRE(english.Read("Babe") == "б+эйб");
  REQUIRE(english.Read("BABE", EnglishReader::Mode::NAME) == "б+аб");
  REQUIRE(english.Read("asabs") == "аз+абс");
  REQUIRE(english.Read("edus") == "эд+ус");
  REQUIRE(english.Read("abera") == "аб+ера");
  REQUIRE(english.Read("aber") == "+абер");
  REQUIRE(english.Read("aser") == "ас+ер");
  REQUIRE(english.Read("er") == "+ер");
  REQUIRE(english.Read("obar") == "+ёбар");
  REQUIRE(english.Read("bbb") == "ббб");
  REQUIRE(english.Read("aby") == "+аби");
  REQUIRE(english.Read("abas") == "аб+ас");
  REQUIRE(english.Read("abare") == "аб+ар");
  REQUIRE(english.Read("bach") == "б+ах");
  REQUIRE(english.Read("batch") == "б+ач");
  REQUIRE(english.Read("bough") == "б+о");
  REQUIRE(english.Read("boughs") == "б+ос");
  REQUIRE(english.Read("asabs").find('+') == english.Read("asabs").rfind('+'));
  REQUIRE(english.Transcribe("Babe") == "B EY1 B");
  REQUIRE(english.Read("zhanna", EnglishReader::Mode::NAME) == "жанна");
  REQUIRE(english.Read("xiaomi", EnglishReader::Mode::NAME) == "сяом+и");
  REQUIRE(english.Read("fischer", EnglishReader::Mode::NAME) == "ф+ишер");
}

TEST_CASE("vits English name records obey UTF-8 and context grammar", "[tts][vits][english]") {
  for (const std::string entry :
       {"@[a]\tA",
        "@[a]\t",
        "@[a]\t+а",
        "@[a]\tА",
        "@[a]\tа1",
        "@[a]\tа ",
        "@[a]\tа\t",
        "@[a]\t\xD0",
        "@[a]\t\xD1\x90",
        "@[1]\tа",
        "@aaaaa[a]\tа",
        "@[a]$$$$$\tа",
        "!\t0",
        "!a!\t0",
        "!er\t-1",
        "!er\t10",
        "!er\tX",
        "!er\tFF",
        "!abcdefghij\t1"}) {
    CAPTURE(entry);
    REQUIRE_THROWS_WITH(MakeReader("#vits english 7 windows 00 stress 4\n" + entry + "\n"), Contains("damaged"));
  }
  const auto permissive = MakeReader("#vits english 7 windows 00 stress 0\n@[a]\tа\n@[b]\tBAD\n", false);
  REQUIRE(permissive.Read("bbab") == "+а");
}

TEST_CASE("vits English names read the learned multigraphs", "[tts][vits][english]") {
  const auto& english = PackageReader();
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"Qurt", "к+урт"},
         {"Wagner", "в+агнер"},
         {"Smith", "см+ит"},
         {"Fergie", "ф+ерги"},
         {"Mukesh", "мук+еш"},
         {"Giraud", "жир+о"},
         {"Perrault", "перр+о"},
         {"Newton", "нь+ютон"},
         {"Poe", "п+о"},
         {"Pasteur", "паст+ер"},
         {"Coldplay", "к+олдплей"}
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word, EnglishReader::Mode::NAME) == expected);
  }
  REQUIRE(english.Read("Yesterday") == "+естэрдэй");
  REQUIRE(english.Read("Believer") == "бэл+ивэр");
}

TEST_CASE("vits English reader rejects every truncated package and trailing bytes", "[tts][vits][english]") {
  const auto reader = CreateDataReader("data/");
  const auto text = ToString(reader->ReadTransientData(kDictionary));
  REQUIRE(text.size() > 5552); // Generated by Python: independent checksum over several Adler blocks.
  for (const bool verify : {false, true}) {
    for (size_t size = 0; size < text.size(); ++size) {
      CAPTURE(size, verify);
      REQUIRE_THROWS_AS(EnglishReader(MakeSpan(text.substr(0, size)), "cut.dict", verify), std::runtime_error);
    }
    for (const std::string tail : {"x", "\n", "garbage\n", "#adler32 00000000"}) {
      REQUIRE_THROWS_AS(MakeRawReader(text + tail, verify), std::runtime_error);
    }
  }
  REQUIRE_THROWS_AS(MakeRawReader(text + "#adler32 00000000\n"), std::runtime_error);
  REQUIRE_THROWS_AS(MakeRawReader(WithChecksum(text)), std::runtime_error);
}

TEST_CASE("vits English whole-word stress exceptions precede suffix rules", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    ">book\t1\n"
    ">textbook\t0\n"
    "text\tT EH1 K.S T\n"
    "textbook\tT EH1 K.S T B UH2 _ K\n"
    "textbooks\tT EH1 K.S T B UH2 _ K S\n"
  );
  REQUIRE(english.Read("textbook") == "т+экстбук");
  REQUIRE(english.Read("textbooks") == "т+экстбукс");
}

TEST_CASE("vits English derives dictionary stems with inflection phonemes", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "&bag\t2\n&box\t2\n&clap\t2\n&close\t2\n&dog\t2\n&dry\t2\n&fold\t2\n&go\t2\n&hope\t2\n"
    "&jump\t2\n&play\t2\n&quick\t2\n&run\t2\n&want\t2\n"
    "bag\tB AE1 G\nbox\tB AA1 K.S\nclap\tK L AE1 P\nclose\tK L OW1 Z _\n"
    "dog\tD AO1 G\ndry\tD R AY1\nfold\tF OW1 L D\ngo\tG OW1\nhope\tHH OW1 P _\njump\tJH AH1 M P\n"
    "play\tP L EY1 _\nquick\tK W IH1 _ K\nrun\tR AH1 N\nwant\tW AA1 N T\n"
  );
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"dogs", "д+огз"},
         {"dog's", "д+огз"},
         {"boxes", "б+оксиз"},
         {"clapped", "кл+эпт"},
         {"closed", "кл+озд"},
         {"wanted", "у+антид"},
         {"folded", "ф+олдид"},
         {"jumped", "дж+ампт"},
         {"played", "пл+эйд"},
         {"hoping", "х+опинг"},
         {"running", "р+анинг"},
         {"runner", "р+анэр"},
         {"runners", "р+анэрз"},
         {"quickly", "кв+икли"},
         {"dried", "др+айд"},
         {"dries", "др+айз"},
         {"bags", "б+эгз"},
         {"going", "г+оуинг"}
       }) {
    CAPTURE(word);
    REQUIRE_FALSE(english.Contains(word));
    REQUIRE(english.Read(word) == expected);
    REQUIRE(CountOf(english.Read(word), '+') == 1);
  }
  REQUIRE(english.Read("replayed").empty()); // One suffix only, with a dictionary stem.
}

TEST_CASE("vits English composes two or three supported dictionary parts", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "&bot\t2\n&chat\t2\n&cloud\t2\n&craft\t2\n&fire\t2\n&fort\t2\n&mine\t2\n"
    "&nite\t2\n&rim\t2\n&sky\t2\n&smart\t2\n&sound\t2\n&watch\t2\n"
    ">cloud\t1\n"
    "bot\tB AA1 T\nchat\tCH _ AE1 T\ncloud\tK L AW1 _ D\ncraft\tK R AE1 F T\nfire\tF AY1 R _\n"
    "fort\tF AO1 R T\nmine\tM AY1 N _\nnite\tN AY1 T _\nrim\tR IH1 M\nsky\tS K AY1\n"
    "smart\tS M AA1 R T\nsound\tS AW1 _ N D\nwatch\tW AA1 T CH _\n"
  );
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"smartwatch", "см+артватч"},
         {"chatbot", "ч+этбот"},
         {"skyrim", "ск+айрим"},
         {"fortnite", "ф+ортнайт"},
         {"minecraft", "м+айнкрафт"},
         {"soundcloud", "саундкл+ауд"},
         {"firechatbot", "ф+айрчатбот"}
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word) == expected);
  }
  REQUIRE(english.Read("firechatbotrim").empty());
  REQUIRE(english.Read("skyxy").empty());
}

TEST_CASE("vits English route scores use boundaries and original capitalization", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "%:initial\t-200\n%:internal\t-300\n%:threshold\t200\n%^ab\t100\n%^abb\t100\n%ab\t100\n%bb$\t100\n"
    "@[a]\tа\n@[b]\tб\n[a]\tAE1\n[b]\tB\n"
    "baba\tB EY1 B _\n"
  );
  REQUIRE(english.Read("abb") == "+эбб");
  REQUIRE(english.Read("Abb") == "+эбб"); // Equality belongs to the English route.
  REQUIRE(english.Read("abB") == "+абб");
  REQUIRE(english.Read("ABB") == "+абб");
  REQUIRE(english.Read("abab") == "+эбаб");
  REQUIRE(english.Read("Abab") == "аб+аб");
  REQUIRE(english.Read("Baab") == "ба+аб");
  REQUIRE(english.Read("abb", EnglishReader::Mode::NAME) == "+абб");
  REQUIRE(english.Read("Baba") == "б+эйб");
  REQUIRE(english.Read("zhanna") == "жанна");
}

TEST_CASE("vits English weak dictionary decompositions still need an English origin", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "%:threshold\t0\n%ab\t-100\n&bab\t1\n"
    "@[a]\tа\n@[b]\tб\n@[s]\tс\nbab\tB EY1 B\n"
  );
  REQUIRE(english.Read("babs") == "б+абс");
  REQUIRE(english.Read("babbab") == "бабб+аб");
  const auto supported = MakeReader(
    "#vits english 7 windows 00 stress 0\n%:threshold\t0\n%ab\t-100\n&bab\t2\n"
    "@[a]\tа\n@[b]\tб\n@[s]\tс\nbab\tB EY1 B\n"
  );
  REQUIRE(supported.Read("babs") == "б+эйбз");
  REQUIRE(supported.Read("babbab") == "б+эйббэйб");
  REQUIRE(supported.Read("babs", EnglishReader::Mode::NAME) == "б+абс");
}

TEST_CASE("vits English origin and family records have a bounded grammar", "[tts][vits][english]") {
  for (const std::string entry :
       {"%a\t1",      "%abcde\t1",   "%^$\t1",   "%a^b\t1",  "%a$b\t1",    "%AB\t1",      "%:other\t1",
        "%ab\t32001", "%ab\t-32001", "%ab\t1.0", "%ab\t+1",  "%ab\t-",     "%ab\t123456", "&a\t2",
        "&a'b\t2",    "&Abc\t2",     "&abc\t0",  "&abc\t-1", "&abc\t1000", "&abc\t01",    "&abc\t"}) {
    CAPTURE(entry);
    REQUIRE_THROWS_WITH(MakeReader("#vits english 7 windows 00 stress 0\n" + entry + "\n"), Contains("damaged"));
  }
  REQUIRE_NOTHROW(MakeReader("#vits english 7 windows 00 stress 0\n%:threshold\t-32000\n%^ab$\t32000\n"));
}

TEST_CASE("vits English name stress uses language endings and their longest match", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "!en\tF\n!hoven\t1\n!on\t0\n!son\tF\n"
    "@[a]\tа\n@[b]\tб\n@[e]\tе\n@[h]\tх\n@[n]\tн\n@[o]\tо\n@[s]\tс\n@[v]\tв\n"
  );
  REQUIRE(english.Read("ababen", EnglishReader::Mode::NAME) == "+абабен");
  REQUIRE(english.Read("abason", EnglishReader::Mode::NAME) == "+абасон");
  REQUIRE(english.Read("abahon", EnglishReader::Mode::NAME) == "абах+он");
  REQUIRE(english.Read("abahoven", EnglishReader::Mode::NAME) == "абах+овен");
  for (const auto& [word, expected] : std::vector<std::pair<const char*, size_t>>{
         {"Reagan", 0},
         {"Biden", 0},
         {"Beethoven", 1},
         {"Lincoln", 0},
         {"Edison", 0},
         {"Orwell", 0},
         {"Coldplay", 0},
         {"Carter", 0}
       }) {
    CAPTURE(word);
    const auto reading = PackageReader().Read(word, EnglishReader::Mode::NAME);
    const auto mark = reading.find('+');
    REQUIRE(mark != std::string::npos);
    size_t vowel = 0;
    for (size_t pos = 0; pos < mark; pos += 2) {
      vowel += std::string("аеёиоуыэюя").find(reading.substr(pos, 2)) != std::string::npos;
    }
    REQUIRE(vowel == expected);
  }
}

TEST_CASE(
  "vits English traditional names precede dictionaries without replacing ordinary words", "[tts][vits][english]"
) {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "=khrushchev\tA хрущ+ёв\n=nice\tN н+ицца\n=rome\tA р+им\n"
    "nice\tN AY1 S _\nrome\tR OW1 M _\n"
  );
  REQUIRE(english.Read("Nice") == "н+айс");
  REQUIRE(english.Read("NICE") == "н+айс");
  REQUIRE(english.Read("nice") == "н+айс");
  REQUIRE(english.Read("nice", EnglishReader::Mode::NAME) == "н+ицца");
  REQUIRE(english.Read("Rome") == "р+им");
  REQUIRE(english.Read("Khrushchev") == "хрущ+ёв");
  REQUIRE(english.Transcribe("Nice") == "N AY1 S");
  REQUIRE(english.Contains("Rome"));
  REQUIRE(PackageReader().Read("Chicago") == "чик+аго");
  REQUIRE(PackageReader().Read("London") == "л+ондон");
  REQUIRE(PackageReader().Read("Paris") == "пар+иж");
  REQUIRE(PackageReader().Read("Yesterday") == "+естэрдэй");
  REQUIRE(PackageReader().Read("Believer") == "бэл+ивэр");
}

TEST_CASE("vits English folds Latin diacritics and preserves the case signal", "[tts][vits][english]") {
  const auto routes = MakeReader(
    "#vits english 7 windows 00 stress 0\n%:threshold\t-100\n&bab\t2\n"
    "@[a]\tа\n@[b]\tб\n@[s]\tс\nbab\tB EY1 B\n"
  );
  REQUIRE(routes.Read("Babs") == "б+эйбз");
  REQUIRE(routes.Read("Bábs") == "б+абс");
  REQUIRE(routes.Read("BábBab") == "бабб+аб");
  REQUIRE(routes.Read("bábs") == "б+абс");
  REQUIRE(routes.Read("Báb") == "б+аб"); // The written accent reaches practical transcription.
  const auto& english = PackageReader();
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"Édith", "эд+ит"},
         {"Édith", "эд+ит"},
         {"Müller", "м+юллер"},
         {"MÜLLER", "м+юллер"},
         {"García", "гарс+ия"},
         {"Reykjavík", "рейкь+явик"},
         {"Genève", "жен+ева"},
         {"Almodóvar", "альмод+овар"},
         {"Márquez", "м+аркес"}
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word) == expected);
  }
  for (const auto& [word, expected] : std::vector<std::pair<const char*, const char*>>{
         {"Dvořák", "дв+оржак"},
         {"Łódź", "л+одзь"},
         {"François", "франсу+а"},
         {"François", "франсу+а"},
         {"Beyoncé", "бей+онсе"},
         {"Beyoncé", "бей+онсе"},
         {"Dijon", "диж+он"},
         {"Peugeot", "пеж+о"},
         {"Renault", "рен+о"},
         {"Citroën", "ситро+ен"},
         {"Nestlé", "нестл+е"},
         {"Pokémon", "покем+он"},
         {"Škoda", "шк+ода"},
         {"Björk", "бь+ёрк"},
         {"Zürich", "ц+юрих"},
         {"São Paulo", "сан-п+аулу"}
       }) {
    CAPTURE(word);
    REQUIRE(english.Read(word) == expected);
  }
  REQUIRE(english.Read(Repeat("é", 40)) == Repeat("е", 38) + "+ее");
  REQUIRE_FALSE(english.Read(Repeat("é", 40)).empty());
  REQUIRE(english.Read(Repeat("é", 40)) == Repeat("е", 38) + "+ее");
  REQUIRE(english.Read(Repeat("ß", 20)) == Repeat("с", 40));
  for (const auto& word : {Repeat("é", 41), Repeat("é", 41), Repeat("ß", 21)}) {
    REQUIRE(english.Read(word).empty());
    REQUIRE(english.Transcribe(word).empty());
    REQUIRE_FALSE(english.Contains(word));
  }
  for (const std::string word : {"́a", "a'́", "a\xC3", "a\xC3!", "a\xE0\x80\x80", "a\xED\xA0\x80", "a😀", "aя"}) {
    CAPTURE(word);
    REQUIRE(english.Read(word).empty());
    REQUIRE(english.Transcribe(word).empty());
    REQUIRE_FALSE(english.Contains(word));
  }
}

TEST_CASE("vits English reads hyphenated components within the shared letter limit", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "=rome\tA р+им\n[a]\tAE1\n[b]\tB\nrome\tR OW1 M _\n"
  );
  REQUIRE(english.Read("ab-ab") == "+эб-+эб");
  REQUIRE(english.Read("Rome-ab") == "р+им-+эб");
  REQUIRE(english.Read("Áb-ab") == "+эб-+эб");
  REQUIRE(english.Read("ab-ab-ab") == "+эб-+эб-+эб");
  REQUIRE(english.Read(std::string(20, 'b') + "-" + std::string(20, 'b')) == Repeat("б", 20) + "-" + Repeat("б", 20));
  for (const std::string word : {"-ab", "ab-", "ab--ab", "ab-1", "ab-я", "ab-'", "ab-é😀"}) {
    CAPTURE(word);
    REQUIRE(english.Read(word).empty());
  }
  REQUIRE(english.Read(std::string(20, 'b') + "-" + std::string(21, 'b')).empty());
  REQUIRE(english.Read(Repeat("b-", 80) + "b").empty());
  REQUIRE(english.Read("ab-c").empty()); // A silent component does not yield a partial word.
  REQUIRE_FALSE(english.Contains("Rome-ab"));
  REQUIRE(english.Transcribe("Rome-ab").empty());
}

TEST_CASE("vits English traditional records require valid letters and vowel stress", "[tts][vits][english]") {
  for (const std::string entry :
       {"=\tA р+им",
        "='\tA р+им",
        "=A\tA р+им",
        "=a-b\tA р+им",
        "=ab\tA рим",
        "=ab\tA +Рим",
        "=ab\tA р+м",
        "=ab\tA р+им+а",
        "=ab\tA р+им1",
        "=ab\tA р+им-",
        "=ab\tA -р+им",
        "=ab\tA р+им--ром",
        "=ab\tA р+им  ром",
        "=ab\tA р+им ",
        "=ab\tA р+им-+",
        "=ab\tA ",
        "=ab\tA р+и\xD0"}) {
    CAPTURE(entry);
    REQUIRE_THROWS_WITH(MakeReader("#vits english 7 windows 00 stress 0\n" + entry + "\n"), Contains("damaged"));
  }
  REQUIRE_NOTHROW(
    MakeReader("#vits english 7 windows 00 stress 0\n=newjersey\tA нью-дж+ерси\n=northdakota\tA северная дак+ота\n")
  );
  const auto damaged = MakeReader("#vits english 7 windows 00 stress 0\n=a\tA BAD\n[a]\tAE1\n", false);
  REQUIRE(damaged.Read("A") == "+э");
}

TEST_CASE(
  "vits English explicit whole words override missing secondary or closing consonants", "[tts][vits][english]"
) {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    ">gameplay\t1\n>login\t1\n"
    "gameplay\tG EY1 M _ P L EY0 _\nlogin\tL AO1 G IH0 N\nlogins\tL AO1 G IH0 N Z\n"
  );
  REQUIRE(english.Read("gameplay") == "гэймпл+эй");
  REQUIRE(english.Read("login") == "лог+ин");
  REQUIRE(english.Read("logins") == "лог+инз");
}

TEST_CASE("vits English bounds phoneme chains before using them", "[tts][vits][english]") {
  const std::string head = "#vits english 7 windows 00 stress 0\n";
  const auto eight = Repeat("AH0.", 7) + "AH0";
  REQUIRE_NOTHROW(MakeReader(head + "a\t" + eight + "\n"));
  for (const auto& key : {"a", "[a]"}) {
    REQUIRE_THROWS_WITH(MakeReader(head + key + "\t" + eight + ".AH0\n"), Contains("damaged"));
  }
  const auto full = Repeat(eight + " ", 15) + eight;
  const auto word = std::string(16, 'a');
  const auto reader = MakeReader(head + word + "\t" + full + "\n");
  REQUIRE(reader.Read(word) == "+" + Repeat("а", 128));
  REQUIRE_THROWS_WITH(MakeReader(head + word + "a\t" + full + " AH0\n"), Contains("damaged"));
  const auto long_chain = "ab\t_ " + Repeat("AH0.", 10000) + "AH0\n";
  REQUIRE_THROWS_WITH(MakeReader(head + long_chain), Contains("damaged"));
  REQUIRE(MakeReader(head + long_chain, false).Read("ab").empty());
}

TEST_CASE("vits English rule limits reject the whole word and permit name fallback", "[tts][vits][english]") {
  const std::string head = "#vits english 7 windows 00 stress 0\n";
  const auto eight = Repeat("AH0.", 7) + "AH0";
  const auto word = std::string(16, 'a');
  const auto rules = "[a]\t" + eight + "\n[b]\tB\n";
  const std::string names = "%:threshold\t0\n@[a]\tа\n@[b]\tб\n";
  for (const bool verify : {true, false}) {
    const auto english = MakeReader(head + rules, verify);
    REQUIRE(english.Read(word) == "+" + Repeat("а", 128));
    for (const auto& overflow : {word + "b", "b" + word}) {
      REQUIRE(english.Read(overflow).empty());
      REQUIRE(english.Transcribe(overflow).empty());
    }
    const auto fallback = MakeReader(head + names + rules, verify);
    REQUIRE(fallback.Read(word + "b") == Repeat("а", 15) + "+аб");
    REQUIRE(fallback.Read("b" + word) == "б" + Repeat("а", 14) + "+аа");
    REQUIRE(fallback.Transcribe(word + "b").empty());
  }
  const auto silent = MakeReader(head + "[a]\t" + eight + "\n[b]\t_\n");
  REQUIRE(silent.Read(word + "b") == "+" + Repeat("а", 128));
}

TEST_CASE("vits English rejects partial readings from unchecked damaged chunks", "[tts][vits][english]") {
  const std::string head = "#vits english 7 windows 00 stress 0\n";
  const std::string names = "%:threshold\t0\n@[a]\tа\n@[b]\tб\n";
  for (const auto& chunk : {Repeat("AH0.", 8) + "AH0", std::string("AH0.XX"), std::string("AH0.")}) {
    const auto rules = "[a]\t" + chunk + "\n[b]\tB\n";
    REQUIRE_THROWS_WITH(MakeReader(head + rules), Contains("damaged"));
    const auto english = MakeReader(head + rules, false);
    REQUIRE(english.Read("bab").empty());
    REQUIRE(english.Transcribe("bab").empty());
    const auto fallback = MakeReader(head + names + rules, false);
    REQUIRE(fallback.Read("bab") == "б+аб");
    REQUIRE(fallback.Transcribe("bab").empty());
    const auto dictionary = MakeReader(head + "[a]\tAE1\n[b]\tB\nba\tB " + chunk + "\n", false);
    REQUIRE_FALSE(dictionary.Contains("ba"));
    REQUIRE(dictionary.Read("ba") == "б+э");
    REQUIRE(dictionary.Transcribe("ba") == "B AE1");
  }
}

TEST_CASE("vits English rejects oversized derived words and compounds", "[tts][vits][english]") {
  const std::string head = "#vits english 7 windows 00 stress 0\n%:threshold\t0\n";
  const auto eight = Repeat("AH0.", 7) + "AH0";
  const auto word = std::string(16, 'a');
  const auto full = Repeat(eight + " ", 15) + eight;
  const auto derived = MakeReader(head + "&" + word + "\t2\n@[a]\tа\n@[s]\tс\n" + word + "\t" + full + "\n");
  REQUIRE(derived.Read(word + "s") == Repeat("а", 15) + "+ас");
  const auto part = std::string(8, 'a');
  const auto compound = MakeReader(
    head + "&" + part + "\t2\n@[a]\tа\n" + part + "\t" + Repeat(eight + " ", 7) + eight + "\n"
  );
  REQUIRE(compound.Read(part + part) == "+" + Repeat("а", 128));
  REQUIRE(compound.Read(part + part + part) == Repeat("а", 22) + "+аа");
}

TEST_CASE("vits English traditional policies distinguish common words and proper names", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "=bath\tN б+ат\n=berlin\tA берл+ин\n=nice\tN н+ицца\n=paris\tU пар+иж\n=turkey\tN т+урция\n"
    "bath\tB AE1 TH _\nberlin\tB ER0 L IH1 N _\nnice\tN AY1 S _\nturkey\tT ER1 _ K IY0 _\n"
  );
  REQUIRE(english.Read("Nice") == "н+айс");
  REQUIRE(english.Read("NICE") == "н+айс");
  REQUIRE(english.Read("Nice", EnglishReader::Mode::NAME) == "н+ицца");
  REQUIRE(english.Read("Turkey") == "т+ёрки");
  REQUIRE(english.Read("Turkey", EnglishReader::Mode::NAME) == "т+урция");
  REQUIRE(english.Read("Bath") == "б+эс");
  REQUIRE(english.Read("Bath", EnglishReader::Mode::NAME) == "б+ат");
  REQUIRE(english.Read("Berlin") == "берл+ин");
  REQUIRE(english.Read("Paris") == "пар+иж");
  const auto known = MakeReader("#vits english 7 windows 00 stress 0\n=nice\tU н+ицца\nnice\tN AY1 S _\n");
  REQUIRE(known.Read("Nice") == "н+айс");
  REQUIRE(known.Read("Nice", EnglishReader::Mode::NAME) == "н+ицца");
}

TEST_CASE("vits English derivation ranks support and checks the restored letter", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n&blue\t2\n&hop\t3\n&hopp\t1\n&hoppe\t9\n"
    "blue\tB L UW1 _\nhop\tHH AA1 P\nhopp\tHH EY1 P _\nhoppe\tHH EY1 P _ AH0\n"
  );
  REQUIRE(english.Read("bluer") == "бл+юэр");
  REQUIRE(english.Read("hopping") == "х+опинг");
  const auto fallback = MakeReader("#vits english 7 windows 00 stress 0\n%:threshold\t0\nhopp\tHH AA1 P _\n");
  REQUIRE(fallback.Read("hopping") == "х+опинг");
}

TEST_CASE("vits English practical transcription preserves accented letters and positions", "[tts][vits][english]") {
  const auto english = MakeReader(
    "#vits english 7 windows 00 stress 0\n"
    "@[a]\tа\n@[ane]$\tан\n@[c]\tк\n@[e]\t_\n@[n]\tн\n@[o]\tо\n@[s]\tс\n@[u]\tу\n"
    "~[ç]\tс\n~[é]\tе\n~[ß]\tсс\n~[ç]\tс\n~[é]\tе\n~[ñ]\tнь\n~[ö]\tё\n~[ü]\tю\n~^[ö]\tо\n~^[ü]\tу\n"
  );
  REQUIRE(english.Read("Aça") == "+аса");
  REQUIRE(english.Read("Aça") == "+аса");
  REQUIRE(english.Read("Aca") == "+ака");
  REQUIRE(english.Read("Ané") == "+ане");
  REQUIRE(english.Read("Ané") == "+ане");
  REQUIRE(english.Read("Aña") == "+аньа");
  REQUIRE(english.Read("Aö") == "а+ё");
  REQUIRE(english.Read("Öa") == "+оа");
  REQUIRE(english.Read("Aü") == "+аю");
  REQUIRE(english.Read("Üa") == "+уа");
  REQUIRE(english.Read("Aßa") == "+асса");
  const auto caron = MakeReader("#vits english 7 windows 00 stress 0\n@[a]\tа\n~[č]\tч\n~[č]\tч\n");
  REQUIRE(caron.Read("Ača") == "+ача");
  REQUIRE(caron.Read("AČa") == "+ача");
  REQUIRE(caron.Read("AČa") == "+ача");
  for (const std::string entry : {"~[ab]\tа", "~[É]\tе", "~[я]\tя", "~[é]\t+е", "~aaaaa[é]\tе", "~[é]\tе"}) {
    REQUIRE_THROWS_WITH(MakeReader("#vits english 7 windows 00 stress 0\n" + entry + "\n"), Contains("damaged"));
  }
}
