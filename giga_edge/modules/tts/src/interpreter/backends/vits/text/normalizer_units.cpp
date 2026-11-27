// The text normalizer: units, money, decimals.
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalization.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits::normalization {

std::optional<std::pair<const Unit*, size_t>> Normalization::MatchUnit(size_t index) const {
  constexpr size_t max_unit_tokens = 6;
  const Unit* best = nullptr;
  size_t end = index;
  std::string abbreviation;
  for (size_t current = index; current < tokens_.size() && current < index + max_unit_tokens; ++current) {
    if (tokens_[current].type == TokenType::NUMBER) {
      break;
    }
    if (current > index && !Glued(current)) {
      abbreviation += ' ';
    }
    abbreviation += tokens_[current].text;
    if (const auto* unit = lexicon_.FindUnit(abbreviation)) {
      best = unit;
      end = current + 1;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  // "5 с половиной", "20% с 1 по 10", "Купил 2 А потом": a one-letter word that is also a preposition or a
  // conjunction is a unit only at the end of a sentence or before a mark.
  const auto& first = tokens_[index];
  constexpr std::string_view letter_words[] = {"с", "в", "к", "о", "у", "а", "и"};
  if (
    end == index + 1 && first.type == TokenType::WORD
    && std::find(std::begin(letter_words), std::end(letter_words), first.lower) != std::end(letter_words)
  ) {
    auto after = end;
    if (IsSymbol(after, ".") && Glued(after)) {
      ++after;
    }
    // "14 мин 26 с": seconds after minutes.
    auto unit_before = index >= 2 && IsNumber(index - 1) ? index - 2 : 0;
    if (unit_before > 0 && IsSymbol(unit_before, ".")) {
      --unit_before;
    }
    const auto* minutes = NounByLemma("минута");
    const auto* previous = unit_before > 0 && IsWord(unit_before)
      ? lexicon_.FindUnit(tokens_[unit_before].text)
      : nullptr;
    const auto after_minutes = previous != nullptr && minutes != nullptr && previous->noun == minutes
      && best->noun != minutes;
    if ((IsWord(after) || IsNumber(after)) && !after_minutes) {
      return std::nullopt;
    }
  }
  return std::make_pair(best, SkipAbbreviationDot(end));
}

size_t Normalization::AddAmount(
  Words& words, const Amount& amount, const std::vector<GrammaticalCase>& governed, bool weak
) {
  if (amount.sign != 0) {
    words.Add(amount.sign < 0 ? "м+инус" : "плюс");
  }
  auto end = amount.end;
  const Unit* unit = amount.units.empty() ? nullptr : amount.units.front();
  const auto add_units = [&](const auto& first_form) {
    if (unit != nullptr) {
      words.Add(first_form(*unit->noun));
      words.Add(unit->tail);
    }
    if (amount.units.size() > 1) {
      words.Add(amount.units[1]->noun->plural[static_cast<int>(kGen)]);
      words.Add(amount.units[1]->tail);
    }
  };
  if (amount.digits.size() > kMaxNumeralDigits) {
    words.Add(ReadDigits(amount.digits));
    if (!amount.fraction.empty()) {
      words.Add("запят+ая");
      words.Add(ReadDigits(amount.fraction));
    }
    add_units([](const Noun& noun) {
      return noun.plural[static_cast<int>(kGen)];
    });
    return end;
  }
  const auto value = ValueOf(amount.digits);
  if (!amount.compound.empty() && !amount.fraction.empty()) {
    // "1,5-литровая": "полуторалитровая".
    words.Add("полутора" + amount.compound);
    return end;
  }
  if (!amount.compound.empty()) {
    if (value >= 1 && value <= 999) {
      // "двухъярусный": a hard sign between a consonant and an iotated vowel.
      auto prefix = CompoundPrefix(value);
      const auto iotated = amount.compound.rfind("я", 0) == 0 || amount.compound.rfind("е", 0) == 0
        || amount.compound.rfind("ё", 0) == 0 || amount.compound.rfind("ю", 0) == 0;
      if (
        iotated && !prefix.empty()
        && std::string_view("аеёиоуыэюя").find(prefix.substr(prefix.size() - 2)) == std::string_view::npos
      ) {
        prefix += "ъ";
      }
      words.Add(prefix + amount.compound);
    } else {
      words.Add(Cardinal(value, kNom, Gender::MASCULINE));
      words.Add(amount.compound);
    }
    return end;
  }
  if (!amount.fraction.empty()) {
    return AddDecimal(words, amount, governed, value);
  }
  NumberContext context;
  context.governed = governed;
  context.governed_weak = weak;
  context.suffix = amount.suffix;
  const Noun* year = amount.year_abbreviation ? NounByLemma("год") : nullptr;
  NounContext noun;
  bool day_part = false;
  if (year != nullptr) {
    // "в 2001 г." is "в ... году", "фильм 2001 г." is "... года", "2001 г. был хорошим", "наступил 2024 г." is "... год".
    context.noun = AnyCase(year->gender, {false});
    const auto verb_before = amount.begin > 0 && IsWord(amount.begin - 1) && (governed.empty() || weak)
      && lexicon_.LooksLikeVerb(tokens_[amount.begin - 1].lower);
    if (!verb_before && (!governed.empty() || (amount.begin > 0 && IsWord(amount.begin - 1)))) {
      context.noun.erase(context.noun.begin() + static_cast<int>(Case::ACCUSATIVE));
      context.noun.erase(context.noun.begin());
    }
    context.ordinal_use = OrdinalUse::OBLIQUE;
  } else if (unit != nullptr) {
    context.noun = AnyCase(unit->noun->gender, {false, true});
    context.ordinal_use = unit->noun->ordinal_use;
  } else {
    noun = NounAfter(amount.end, true, CountFormOf(value) != CountForm::ONE);
    context.noun = noun.readings;
    context.noun_guessed = noun.noun == nullptr && !noun.known;
    // "в 2 ночи", "в 5 утра", "с 1 до 4 ночи": hours of a part of the day.
    if ((noun.position != 0 && IsDayPart(noun.position)) || noun.day_part) {
      context.noun.clear();
      day_part = true;
    }
    context.ordinal_use = noun.ordinal_use;
    constexpr uint64_t max_day = 31;
    if (noun.month >= 0 && value >= 1 && value <= max_day) {
      const auto after = noun.position == 0 ? amount.end : DateEnd(noun.position);
      words.Add(ReadDay(value, amount.begin, after, governed, amount.suffix));
      if (noun.position == 0) {
        return end;
      }
      if (noun.month_abbreviation) {
        words.Add(kMonths[static_cast<size_t>(noun.month)]);
        return AddYear(words, SkipAbbreviationDot(noun.position + 1), false);
      }
      words.Add(tokens_[noun.position].lower);
      return AddYear(words, noun.position + 1, false);
    }
    if (noun.month >= 0) {
      context.noun = Guess(tokens_[noun.position].lower);
    }
    if (IsLabel(amount.begin) && amount.suffix.empty()) {
      // "лампа 2", "у лампы 3 установлен": a name in the nominative; only a known noun after it counts.
      context.governed.clear();
      if (noun.noun == nullptr) {
        context.noun.clear();
        context.ordinal_use = OrdinalUse::NONE;
      }
    } else if (context.noun.empty() && amount.suffix.empty() && counted_gender_) {
      // "у вас 21 устройство, из них 1 не в сети": the noun of the number before.
      context.noun = AnyCase(*counted_gender_, {false, true});
    }
    // "это было в 2024": a year without its noun.
    constexpr uint64_t max_bare_year = 2100;
    if (
      amount.digits.size() == 4 && value >= kMinCalendarYear && value <= max_bare_year && noun.readings.empty()
      && noun.month < 0 && amount.suffix.empty() && amount.begin > 0 && IsWord(amount.begin - 1)
      && (tokens_[amount.begin - 1].lower == "в" || tokens_[amount.begin - 1].lower == "во")
    ) {
      words.Add(Ordinal(value, Case::PREPOSITIONAL, Gender::MASCULINE));
      return end;
    }
    // "в 80-е", "1990-х": decades are plural.
    constexpr uint64_t ten = 10;
    if (!amount.suffix.empty() && value >= 2 * ten && value % ten == 0 && noun.readings.empty()) {
      context.noun = AnyCase(Gender::MASCULINE, {true});
      context.noun_guessed = false;
    }
    // "остался 1 этаж", "есть 1 свободное место": a count, not a number in order.
    if (
      context.ordinal_use == OrdinalUse::ALWAYS && amount.suffix.empty()
      && (noun.adjective || IsCountingWord(amount.begin))
    ) {
      context.ordinal_use = OrdinalUse::NONE;
    }
  }
  const auto reading = Agree(value, context);
  // The gender of the last counted noun: a known noun gives it, a singular form shows it, a plural form of an unknown
  // noun does not and forgets the one before.
  const auto agrees = std::any_of(context.noun.begin(), context.noun.end(), [&](const NounReading& noun_reading) {
    return !noun_reading.plural && Fits(value, reading, noun_reading);
  });
  const auto own_noun = unit == nullptr && year == nullptr && !noun.readings.empty() && noun.month < 0;
  if (!reading.ordinal && own_noun && noun.noun != nullptr) {
    counted_gender_ = noun.noun->gender;
  } else if (!reading.ordinal && agrees) {
    counted_gender_ = reading.gender;
  } else if (!reading.ordinal && own_noun) {
    counted_gender_.reset();
  }
  auto number = ReadNumber(value, reading);
  if (!reading.ordinal && (noun.noun != nullptr || noun.plural_only)) {
    number = CountOfNoun(noun.noun != nullptr ? noun.noun->plural_only : noun.plural_only, value, reading, number);
  }
  // "нас было 3", "вас осталось 4": persons without a noun are counted by a collective numeral.
  if (
    !reading.ordinal && noun.readings.empty() && unit == nullptr && year == nullptr && noun.month < 0
    && amount.suffix.empty() && amount.fraction.empty() && value >= 2 && value <= 7 && CountsPersons(amount.begin)
  ) {
    constexpr std::array<std::string_view, 6> persons_list = {
      "дв+ое", "тр+ое", "ч+етверо", "п+ятеро", "ш+естеро", "с+емеро"
    };
    number = persons_list[value - 2];
  }
  // "в час ночи", "с часу до четырёх ночи".
  constexpr std::array<std::string_view, kCaseCount> one_hour = {"час", "ч+асу", "ч+асу", "час", "ч+асом", "час+е"};
  if (day_part && value == 1 && !reading.ordinal && amount.suffix.empty() && amount.fraction.empty()) {
    number = one_hour[static_cast<size_t>(reading.grammatical_case)];
  }
  words.Add(number);
  if (year != nullptr) {
    words.Add(
      amount.year_plural ? year->plural[static_cast<int>(reading.grammatical_case)] : CountedNoun(*year, value, reading)
    );
  }
  add_units([&](const Noun& unit_noun) {
    return CountedNoun(unit_noun, value, reading);
  });
  // "2 минуты": a number and a known noun right after it are one group.
  if (own_noun && noun.noun != nullptr && noun.position == amount.end && !noun.adjective) {
    words.Add(tokens_[noun.position].lower);
    return noun.position + 1;
  }
  return end;
}

std::string Normalization::CountOfNoun(
  bool plural_only, uint64_t value, const NumeralReading& reading, std::string number
) {
  const auto case_index = static_cast<int>(reading.grammatical_case);
  const auto direct = reading.grammatical_case == kNom || reading.grammatical_case == Case::ACCUSATIVE;
  const auto last_word = number.rfind(' ') == std::string::npos ? 0 : number.rfind(' ') + 1;
  if (plural_only && CountFormOf(value) == CountForm::ONE) {
    constexpr std::array<std::string_view, kCaseCount> one_plural = {
      "одн+и",
      "одн+их",
      "одн+им",
      "одн+и",
      "одн+ими",
      "одн+их",
    };
    return number.substr(0, last_word) + std::string(one_plural[case_index]);
  }
  if (plural_only && direct && value >= 2 && value <= 4) {
    constexpr std::array<std::string_view, 3> collective_list = {"дв+ое", "тр+ое", "ч+етверо"};
    return std::string(collective_list[value - 2]);
  }
  return number;
}

std::string Normalization::AgreeAdjective(
  const std::string& word, uint64_t count, const NumeralReading& reading, Gender gender
) {
  // The written genitive singular gives the stem: "белорусского", "синего", "нового".
  constexpr std::string_view velar_letters = "кгхжшчщ";
  for (const std::string_view ending : {"ого", "его"}) {
    if (word.size() <= ending.size() || word.compare(word.size() - ending.size(), ending.size(), ending) != 0) {
      continue;
    }
    const auto stem = word.substr(0, word.size() - ending.size());
    const auto soft = ending == "его";
    const auto velar = !soft && stem.size() >= 2
      && velar_letters.find(stem.substr(stem.size() - 2)) != std::string::npos;
    const auto i = soft || velar;
    // Nominative, genitive, dative, accusative (of the inanimate), instrumental, prepositional.
    const std::array<std::string_view, kCaseCount> masculine = {
      i ? "ий" : "ый",
      soft ? "его" : "ого",
      soft ? "ему" : "ому",
      i ? "ий" : "ый",
      i ? "им" : "ым",
      soft ? "ем" : "ом",
    };
    const std::array<std::string_view, kCaseCount> feminine = {
      soft ? "яя" : "ая",
      soft ? "ей" : "ой",
      soft ? "ей" : "ой",
      soft ? "юю" : "ую",
      soft ? "ей" : "ой",
      soft ? "ей" : "ой",
    };
    const std::array<std::string_view, kCaseCount> plural = {
      i ? "ие" : "ые",
      i ? "их" : "ых",
      i ? "им" : "ым",
      i ? "ие" : "ые",
      i ? "ими" : "ыми",
      i ? "их" : "ых",
    };
    const auto case_index = static_cast<size_t>(reading.grammatical_case);
    const auto direct = reading.grammatical_case == kNom || reading.grammatical_case == Case::ACCUSATIVE;
    if (CountFormOf(count) == CountForm::ONE) {
      return stem + std::string((gender == Gender::FEMININE ? feminine : masculine)[case_index]);
    }
    if (!direct) {
      return stem + std::string(plural[case_index]);
    }
    // "два белорусских рубля", "две белорусские копейки", "пять белорусских рублей".
    const auto few_feminine = CountFormOf(count) == CountForm::FEW && gender == Gender::FEMININE;
    return stem + std::string(few_feminine ? plural[0] : plural[static_cast<size_t>(kGen)]);
  }
  return word;
}

size_t Normalization::AddDecimal(
  Words& words, const Amount& amount, const std::vector<GrammaticalCase>& governed, uint64_t value
) {
  auto end = amount.end;
  const Unit* unit = amount.units.empty() ? nullptr : amount.units.front();
  // Money: "92,35 рубл+я" -> "девян+осто два рубл+я, тр+идцать пять коп+еек".
  const Noun* currency = nullptr;
  NounContext currency_word;
  if (unit != nullptr && amount.units.size() == 1 && unit->noun->subunit != nullptr) {
    currency = unit->noun;
  } else if (amount.units.empty()) {
    currency_word = NounAfter(amount.end, false);
    if (currency_word.noun != nullptr && currency_word.noun->subunit != nullptr && currency_word.position != 0) {
      currency = currency_word.noun;
    }
  }
  constexpr size_t cent_digits = 2;
  if (currency != nullptr && amount.fraction.size() == cent_digits) {
    // The case of the written currency unless it is the genitive after a fraction: "с 1,20 рубл+ями".
    auto cases = governed;
    std::vector<GrammaticalCase> written;
    for (const auto& reading : currency_word.readings) {
      if (reading.grammatical_case != kGen) {
        written.push_back(reading.grammatical_case);
      }
    }
    for (const auto grammatical_case : governed.empty() ? written : governed) {
      if (std::find(written.begin(), written.end(), grammatical_case) != written.end()) {
        cases = {grammatical_case};
        break;
      }
    }
    for (const auto& [count, noun] :
         {std::pair{value, currency}, std::pair{ValueOf(amount.fraction), currency->subunit}}) {
      // "0,02 руб." is "две коп+ейки", "15.00 руб." is "пятн+адцать рубл+ей".
      if (count == 0 && (noun == currency->subunit || ValueOf(amount.fraction) != 0)) {
        continue;
      }
      if (noun == currency->subunit && value != 0) {
        words.Add(",");
      }
      NumberContext context;
      context.governed = cases;
      context.noun = AnyCase(noun->gender, {false, true});
      const auto reading = Agree(count, context);
      words.Add(ReadNumber(count, reading));
      // "1,50 белорусского рубля" -> "од+ин белорусский рубль", "0,50 белорусского рубля" -> "пятьдес+ят белорусских
      // коп+еек": the words of the currency go with the first noun read, never left out.
      if (currency_word.adjective && (noun == currency || value == 0)) {
        for (auto word = amount.end; word < currency_word.position; ++word) {
          words.Add(AgreeAdjective(tokens_[word].lower, count, reading, noun->gender));
        }
      }
      words.Add(CountedNoun(*noun, count, reading));
    }
    if (unit != nullptr) {
      words.Add(unit->tail);
    }
    return currency_word.position != 0 ? currency_word.position + 1 : end;
  }
  const auto grammatical_case = governed.empty() ? kNom : governed.front();
  // "1,5 час+а" -> "полтор+а час+а", "до 1,5 часа" -> "до пол+утора час+ов", "в 1,5 метрах" -> "в пол+утора
  // метрах": with a noun, not alone; the case of the noun if the governing word allows it.
  if (value == 1 && amount.fraction == "5" && amount.units.size() <= 1) {
    const Noun* noun = unit != nullptr ? unit->noun : nullptr;
    auto noun_case = grammatical_case;
    std::optional<Gender> gender;
    size_t noun_end = end;
    if (noun == nullptr) {
      const auto after = NounAfter(amount.end, false);
      for (const auto& reading : after.readings) {
        const auto allowed = governed.empty() || reading.grammatical_case == kGen
          || std::find(governed.begin(), governed.end(), reading.grammatical_case) != governed.end();
        if ((reading.plural || reading.grammatical_case == kGen) && allowed) {
          gender = reading.gender;
          if (reading.plural && reading.grammatical_case != kGen) {
            noun_case = reading.grammatical_case;
          }
          break;
        }
      }
      if (after.noun != nullptr && after.position == amount.end) {
        noun = after.noun;
        noun_end = after.position + 1;
      }
    } else {
      gender = noun->gender;
    }
    if (gender) {
      const auto direct = noun_case == kNom || noun_case == Case::ACCUSATIVE;
      words.Add(direct ? (*gender == Gender::FEMININE ? "полтор+ы" : "полтор+а") : "пол+утора");
      if (noun != nullptr) {
        words.Add(direct ? noun->count : noun->plural[static_cast<int>(noun_case)]);
        words.Add(unit != nullptr ? unit->tail : "");
        return noun_end;
      }
      return end;
    }
  }
  if (amount.fraction.size() <= static_cast<size_t>(kMaxFractionDigits)) {
    // A pause after the whole part helps to hear the two numbers: "пятн+адцать ц+елых, шесть дес+ятых".
    auto decimal = Decimal(value, amount.fraction, grammatical_case);
    const auto whole_word = decimal.find(" ц+ел") + 1;
    const auto whole_end = decimal.find(' ', whole_word);
    // After zero the genitive plural in every case: "нол+ю ц+елых".
    if (value == 0) {
      decimal.replace(whole_word, whole_end - whole_word, "ц+елых");
    }
    decimal.insert(decimal.find(' ', whole_word), ",");
    words.Add(decimal);
  } else {
    words.Add(Cardinal(value, grammatical_case, Gender::MASCULINE));
    words.Add("запят+ая");
    words.Add(ReadDigits(amount.fraction));
  }
  if (unit != nullptr) {
    words.Add(NounAfterDecimal(*unit->noun));
    words.Add(unit->tail);
  }
  if (amount.units.size() > 1) {
    words.Add(amount.units[1]->noun->plural[static_cast<int>(kGen)]);
    words.Add(amount.units[1]->tail);
  }
  return end;
}

} // namespace giga_edge::vits::normalization
