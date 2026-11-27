#include "giga_edge/modules/tts/interpreter/backends/vits/text/foreign.h"

#include <catch.hpp>

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace giga_edge::vits;

TEST_CASE("vits foreign words are read as they are written", "[tts][vits][foreign]") {
  for (const auto& [word, russian] : std::vector<std::pair<std::string, std::string>>{
         // German: sch, tsch, st/sp of a stem, ei, ie, eu, au, z, tz, v, j, s before a vowel at the start; the stress
         // on the last diphthong, else on the first vowel.
         {"rammstein", "раммшт+айн"},
         {"eschenbach", "+эшенбах"},
         {"fischer", "ф+ишер"},
         {"mischke", "м+ишке"},
         {"zhanna", "жанна"},
         {"zhenya", "женя"},
         {"medvedev", "медведев"},
         {"nabokov", "набоков"},
         {"zhukov", "жуков"},
         {"zhang", "чж+ан"},
         {"zhao", "чж+ао"},
         {"shenzhen", "шэнчж+эн"},
         {"shanghai", "шанх+ай"},
         {"steinmetz", "шт+айнмец"},
         {"schumacher", "ш+умахер"},
         {"sonnenschein", "зонненш+айн"},
         {"tschaikmann", "ч+айкманн"},
         {"spitzberg", "шп+ицберг"},
         {"freudenberg", "фр+ойденберг"},
         {"augsburg", "+аугсбург"},
         {"zaubermann", "ц+ауберманн"},
         {"vogelheim", "фогелх+айм"},
         {"jentsch", "й+енч"},
         {"bachmann", "б+ахманн"},
         {"lentz", "л+енц"},
         {"gesaffelstein", "гезаффелшт+айн"},
         // Chinese: x, q, zh, c, z, j, ia, iao, ie, iu, e; the stress on the last syllable.
         {"xiaomi", "сяом+и"},
         {"jiang", "цз+ян"},
         {"guangzhou", "гуанчж+оу"},
         {"xuezhong", "суэчж+он"},
         {"qiang", "ц+ян"},
         {"xiejiu", "сецз+ю"},
         {"xiecaoze", "сецаоцз+э"},
         // Slavic spelling is returned without stress for the downstream Russian stress stage.
         {"rakhim", "рахим"},
         {"murovei", "муровей"},
         {"ivanov", "иванов"},
         {"khodorkovsky", "ходорковский"},
         {"trotsky", "троцкий"},
         {"tchaikovsky", "чайковский"},
         {"petrovskaya", "петровская"},
         {"ilyushin", "ильюшин"},
         {"ivanova", "иван+ова"},
         {"dostoyevsky", "достоевский"},
         {"vysotskaya", "высоцкая"},
         {"krasnyy", "красный"},
         {"dmitriy", "дмитрий"},
         {"yurkov", "юрков"},
         {"kazakh", "казах"},
         {"pyotrov", "пьётров"},
         // Open syllables: consonant-vowel pairs ending in a vowel other than 'e'; the stress on the vowel before the
         // last one.
         {"miyagi", "ми+яги"},
         {"openai", "оп+енай"},
         {"tokyo", "ток+ё"},
         {"ryoko", "р+ёко"},
         {"pasta", "п+аста"},
         {"niletto", "нил+етто"},
         {"meizu", "м+ейзу"},
         {"sakura", "сак+ура"},
         {"ferrari", "ферр+ари"},
         {"lamborghini", "ламборг+ини"},
         {"mitsubishi", "мицуб+иши"},
         {"jumanji", "джум+анджи"},
         {"yakuza", "як+уза"},
         {"okinawa", ""},
         {"zara", "з+ара"},
         {"elena", "эл+ена"},
       }) {
    CAPTURE(word);
    REQUIRE(ReadForeign(word) == russian);
  }
}

TEST_CASE("vits foreign words leave English ones alone", "[tts][vits][foreign]") {
  for (const std::string word :
       {"",
        "a",
        "coldplay",
        "weeknd",
        "loqiemean",
        "spotify",
        "queen",
        "don't",
        "sch'ool",
        "panda'",
        "bark",
        "barkhurst",
        "dykhouse",
        "maiu",
        "khrome",
        "kh",
        "khz",
        "tomate",
        "strada",
        "theta",
        "photo",
        "taboo",
        "tequila",
        "ov",
        "lov",
        "sky",
        "ei",
        "bluetooth",
        "windows",
        "mischievous",
        "eschewing",
        "schematic",
        "improv",
        "afterschool",
        "schoolhouse",
        "unscheduled",
        "discharge",
        "dischargeable",
        "preschooling",
        "interschool",
        "triangulating",
        "xiphoids",
        "qiwi",
        "quartzite",
        "husky",
        "whisky",
        "ana"}) {
    CAPTURE(word);
    REQUIRE(ReadForeign(word).empty());
  }
}

TEST_CASE("vits foreign words of Russian names override the dictionary only by sure signs", "[tts][vits][foreign]") {
  for (const char* word : {
         "zhanna", "zhenya", "smirnov", "smirnova", "dostoyevsky", "trotsky", "khrushchev", "zhukov", "rakhim", "kazakh"
       }) {
    CAPTURE(word);
    REQUIRE(IsSurelyForeign(word));
  }
  // Weak signs, other languages, English words and too short endings.
  for (const char* word : {"improv",   "zhao",    "alzheimer", "fitzhugh",  "itzhak",    "steakhouse", "brookhaven",
                           "casanova", "geneva",  "villanova", "zhang",     "shenzhen",  "ivanova",    "murovei",
                           "yuri",     "dmitriy", "miyagi",    "xiaomi",    "rammstein", "husky",      "whisky",
                           "nova",     "lev",     "sky",       "barkhurst", "",          "smirnov's"}) {
    CAPTURE(word);
    REQUIRE_FALSE(IsSurelyForeign(word));
  }
}

TEST_CASE("vits Slavic readings defer stress to the Russian stage", "[tts][vits][foreign]") {
  for (const auto* word : {"sokolov", "popov", "kuznetsov", "ivanov", "petrov", "smirnov", "zhanna", "rakhim"}) {
    const auto reading = ReadForeign(word);
    REQUIRE_FALSE(reading.empty());
    REQUIRE(reading.find('+') == std::string::npos);
  }
  REQUIRE(ReadForeign("improv").empty());
}
