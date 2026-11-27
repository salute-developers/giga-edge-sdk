#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace giga_edge::vits {

namespace {

constexpr char kDictionaryFile[] = "stress.dict";
constexpr char kYoFile[] = "yo.dict";
constexpr char kYoExtraFile[] = "yo_extra.tsv";
constexpr char kHomographsFile[] = "homographs.tsv";
constexpr char kOverridesFile[] = "stress_overrides.tsv";
constexpr char kHyphenFile[] = "hyphen.tsv";
constexpr char kNoPauseFile[] = "no_pause_words.txt";
constexpr char kEToEhFile[] = "e_to_eh.tsv";
constexpr char kModelFile[] = "stress.onnx";
constexpr char kModelVocabularyFile[] = "stress_vocab.txt";
constexpr char kHomographModelFile[] = "homographs.onnx";
constexpr char kSecondHomographModelFile[] = "homographs2.onnx";
constexpr char kHomographVariantsFile[] = "homographs_variants.tsv";
constexpr char kMarkedWordInput[] = "homo_start_ids"; // an input of a model of a marked word only
constexpr char kHomographVocabularyFile[] = "homographs_vocab.txt";
constexpr char kSecondHomographVocabularyFile[] = "homographs2_vocab.txt";
constexpr char kHomographPriorsFile[] = "homographs_priors.tsv";
constexpr char kHomographReadingsFile[] = "homographs.dict";

void SetVariant(StressItem& part, std::string_view variant, StressSource source) {
  part.text = WithoutMarks(variant);
  part.stressed = std::string(variant);
  part.source = source;
}

void MarkLetters(StressItem& part, const StressDict::Marks& marks, StressSource source) {
  part.stressed.clear();
  for (size_t i = 0; i < LetterCount(part.text); ++i) {
    if (i < marks.size() && marks.test(i) && IsVowel(LetterAt(part.text, i))) {
      part.stressed += '+';
    }
    part.stressed.append(part.text, i * kLetterBytes, kLetterBytes);
  }
  part.source = source;
}

// An entry of a dictionary that marks no vowel of the word is no entry: the word goes on to the next stage.
bool MarksVowel(std::string_view word, const StressDict::Marks& marks) {
  for (size_t i = 0; i < LetterCount(word) && i < marks.size(); ++i) {
    if (marks.test(i) && IsVowel(LetterAt(word, i))) {
      return true;
    }
  }
  return false;
}

void MarkLetter(StressItem& part, size_t letter, StressSource source) {
  part.stressed = part.text;
  part.stressed.insert(letter * kLetterBytes, 1, '+');
  part.source = source;
}

size_t CountVowels(std::string_view word) {
  size_t vowels = 0;
  for (size_t i = 0; i < LetterCount(word); ++i) {
    vowels += IsVowel(LetterAt(word, i)) ? 1 : 0;
  }
  return vowels;
}

// A word with its marks that may have hyphens between its parts.
bool IsStressedHyphenated(std::string_view stressed) {
  const auto parts = Split(stressed, '-');
  return std::all_of(parts.begin(), parts.end(), IsStressedWord);
}

} // namespace

std::string StressedText(const std::vector<StressItem>& items) {
  std::string text;
  for (const auto& item : items) {
    text += item.stressed;
  }
  return text;
}

Stresser::Stresser(
  const StressParams& params,
  const std::string& directory,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
) {
  const std::pair<const char*, double> confidences[] = {
    {"homograph_confidence", params.homograph_confidence},
    {"homograph_strong_confidence", params.homograph_strong_confidence},
    {"homograph_mild_confidence", params.homograph_mild_confidence},
  };
  for (const auto& [name, confidence] : confidences) {
    if (!(confidence >= 0.0 && confidence <= 1.0)) {
      throw std::invalid_argument(std::string("vits: ") + name + " must be from 0 to 1");
    }
  }
  const auto path = [&directory](const std::string& name) {
    return directory.empty() ? name : directory + "/" + name;
  };
  // A table that is not in the package is skipped.
  const auto for_each_row = [&](const std::string& name, size_t min_fields, const auto& handle) {
    const auto span = data_reader->ReadTransientData(path(name));
    if (!span) {
      return;
    }
    const auto data = ToString(*span);
    for (const auto& row : ReadTable(data)) {
      if (row.fields.size() < min_fields || row.fields[0].empty()) {
        ThrowTableError(name, row.line, "expected " + std::to_string(min_fields) + " tab-separated fields");
      }
      handle(row);
    }
  };

  dictionary_.emplace(
    EnsureNotNull(data_reader->ReadPersistentData(path(kDictionaryFile)), path(kDictionaryFile)),
    kDictionaryFile,
    params.verify_dictionaries
  );
  if (auto span = data_reader->ReadPersistentData(path(kYoFile))) {
    yo_.emplace(std::move(span), kYoFile, params.verify_dictionaries);
  }
  for_each_row(kYoExtraFile, 2, [&](const TableRow& row) {
    const auto word = row.fields[0];
    const auto with_yo = row.fields[1];
    if (!IsRussianWord(with_yo) || FoldYo(with_yo) != FoldYo(word)) {
      ThrowTableError(
        kYoExtraFile, row.line, "'" + std::string(with_yo) + "' is not '" + std::string(word) + "' with 'ё'"
      );
    }
    yo_extra_.insert_or_assign(std::string(word), std::string(with_yo));
  });
  for_each_row(kOverridesFile, 2, [&](const TableRow& row) {
    const auto word = row.fields[0];
    const auto stressed = row.fields[1];
    const auto letters = FoldYo(WithoutMarks(stressed));
    const auto parts = Split(letters, '-');
    // A word with a vowel is given with its stress.
    const bool has_stress = stressed.find('+') != std::string_view::npos
      || std::none_of(parts.begin(), parts.end(), [](std::string_view part) {
                              return CountVowels(part) > 0;
                            });
    if (!IsStressedHyphenated(stressed) || letters != FoldYo(word) || !has_stress) {
      ThrowTableError(
        kOverridesFile,
        row.line,
        "'" + std::string(stressed) + "' is not '" + std::string(word) + "' with '+' before the stressed vowels"
      );
    }
    overrides_.insert_or_assign(std::string(word), std::string(stressed));
  });
  // e_to_eh: "@NAME<TAB>endings by commas" is a class of endings; "stem<TAB>stem with 'э'[<TAB>endings and @NAME
  // classes by commas]" lists the forms stem + ending that read so; without the third field it is the stem alone.
  // Only the 'е' of the stem change; a form stands in the table once.
  std::map<std::string, std::vector<std::string>, std::less<>> ending_classes;
  for_each_row(kEToEhFile, 2, [&](const TableRow& row) {
    const auto fail = [&](const std::string& what) {
      ThrowTableError(kEToEhFile, row.line, what);
    };
    const auto stem = row.fields[0];
    const auto hard = row.fields[1];
    if (stem.front() == '@') {
      std::vector<std::string> endings;
      for (const auto ending : Split(hard, ',')) {
        endings.emplace_back(ending);
      }
      if (!ending_classes.emplace(std::string(stem), std::move(endings)).second) {
        fail("the class '" + std::string(stem) + "' is repeated");
      }
      return;
    }
    bool same = IsRussianWord(stem) && IsRussianWord(hard) && stem.size() == hard.size() && stem != hard;
    for (size_t i = 0; same && i < LetterCount(stem); ++i) {
      const auto letter = LetterAt(stem, i);
      same = LetterAt(hard, i) == letter || (letter == U'е' && LetterAt(hard, i) == U'э');
    }
    if (!same) {
      fail("'" + std::string(hard) + "' is not '" + std::string(stem) + "' with 'э' for 'е'");
    }
    std::vector<std::string> endings;
    for (const auto item : Split(row.fields.size() > 2 ? row.fields[2] : std::string_view(), ',')) {
      if (item.empty() || item.front() != '@') {
        endings.emplace_back(item);
      } else if (const auto found = ending_classes.find(item); found != ending_classes.end()) {
        endings.insert(endings.end(), found->second.begin(), found->second.end());
      } else {
        fail("unknown class '" + std::string(item) + "'");
      }
    }
    for (const auto& ending : endings) {
      if (!ending.empty() && !IsRussianWord(ending)) {
        fail("'" + ending + "' is not an ending");
      }
      if (!e_to_eh_.emplace(std::string(stem) + ending, std::string(hard) + ending).second) {
        fail("the word '" + std::string(stem) + ending + "' is already in the table");
      }
    }
  });
  if (const auto span = data_reader->ReadTransientData(path(kHomographsFile))) {
    homographs_.emplace(ToString(*span), kHomographsFile);
  }
  for_each_row(kHyphenFile, 2, [&](const TableRow& row) {
    const auto kind = row.fields[0];
    if (kind == "prefix") {
      prefixes_.emplace(row.fields[1]);
    } else if (kind == "suffix") {
      suffixes_.emplace(row.fields[1]);
    } else if (kind == "letters") {
      for (const auto name : Split(row.fields[1], ',')) {
        letter_names_.emplace(name);
      }
    } else if (kind == "yo") {
      if (!IsRussianWord(row.fields[1]) || !HasYo(row.fields[1])) {
        ThrowTableError(kHyphenFile, row.line, "'" + std::string(row.fields[1]) + "' is not a word with 'ё'");
      }
      yo_first_.insert_or_assign(FoldYo(row.fields[1]), std::string(row.fields[1]));
    } else {
      ThrowTableError(
        kHyphenFile, row.line, "unknown kind '" + std::string(kind) + "': expected prefix, suffix, letters or yo"
      );
    }
  });
  for_each_row(kNoPauseFile, 1, [&](const TableRow& row) {
    std::vector<std::string> phrase;
    for (const auto by_space : Split(row.fields[0], ' ')) {
      for (const auto word : Split(by_space, '-')) {
        if (!word.empty()) {
          phrase.push_back(FoldYo(word));
        }
      }
    }
    if (!phrase.empty()) {
      no_pause_.push_back(std::move(phrase));
    }
  });
  std::stable_sort(no_pause_.begin(), no_pause_.end(), [](const auto& left, const auto& right) {
    return left.size() > right.size();
  });
  for_each_row(kHomographPriorsFile, 3, [&](const TableRow& row) {
    const auto word = row.fields[0];
    const auto reading = row.fields[1];
    const auto strength = row.fields[2];
    const bool is_reading = IsRussianWord(word) && !HasYo(word) && IsStressedWord(reading)
      && std::count(reading.begin(), reading.end(), '+') == 1 && FoldYo(WithoutMarks(reading)) == word;
    const bool is_strength = strength == "strong" || strength == "mild" || strength == "none";
    const double confidence = strength == "strong"
      ? params.homograph_strong_confidence
      : (strength == "mild" ? params.homograph_mild_confidence : 0.0);
    if (
      !is_reading || !is_strength || !priors_.emplace(std::string(word), Prior{std::string(reading), confidence}).second
    ) {
      ThrowTableError(kHomographPriorsFile, row.line, "expected a new word, its reading and strong, mild or none");
    }
  });
  const auto open = [&](const char* name) {
    return OpenTextModel(path(name), params.environment, data_reader, environments, params.mmap_models);
  };
  if (data_reader->ReadPersistentData(path(kModelFile))) {
    model_ = std::make_unique<StressModel>(open(kModelFile), path(kModelVocabularyFile), data_reader);
  }
  if (data_reader->ReadPersistentData(path(kHomographModelFile))) {
    std::vector<TextModel> models{open(kHomographModelFile)};
    const auto inputs = models[0].session->InputNames();
    if (std::find(inputs.begin(), inputs.end(), kMarkedWordInput) != inputs.end()) {
      homograph_classifier_ = std::make_unique<HomographClassifier>(
        models[0], path(kHomographVocabularyFile), path(kHomographVariantsFile), data_reader
      );
      // The small model next to the big one: a model of pairs with its own vocabulary.
      if (data_reader->ReadPersistentData(path(kSecondHomographModelFile))) {
        models[0] = open(kSecondHomographModelFile);
        homograph_model_ = std::make_unique<HomographModel>(models, path(kSecondHomographVocabularyFile), data_reader);
      }
    } else {
      if (data_reader->ReadPersistentData(path(kSecondHomographModelFile))) {
        models.push_back(open(kSecondHomographModelFile));
      }
      homograph_model_ = std::make_unique<HomographModel>(models, path(kHomographVocabularyFile), data_reader);
    }
    if (auto span = homograph_model_ ? data_reader->ReadPersistentData(path(kHomographReadingsFile)) : nullptr) {
      homograph_readings_.emplace(std::move(span), kHomographReadingsFile, params.verify_dictionaries);
    }
    homograph_mode_ = params.homographs;
    homograph_confidence_ = params.homograph_confidence;
  }
}

std::vector<StressItem> Stresser::Parse(std::string_view text) const {
  // A '+' is a stress only before a vowel: any other one is dropped before the words are found.
  std::string without_stray_marks;
  if (text.find('+') != std::string_view::npos) {
    for (size_t pos = 0; pos < text.size();) {
      const auto size = DecodeUtf8(text, pos).size;
      const auto next = pos + size;
      if (text[pos] != '+' || (next < text.size() && IsVowel(DecodeUtf8(text, next).value))) {
        without_stray_marks.append(text, pos, size);
      }
      pos = next;
    }
    text = without_stray_marks;
  }

  struct Symbol {
    char32_t value = 0;
    size_t begin = 0;
  };
  std::vector<Symbol> symbols;
  symbols.reserve(text.size());
  for (size_t pos = 0; pos < text.size();) {
    const auto symbol = DecodeUtf8(text, pos);
    symbols.push_back({symbol.value, pos});
    pos += symbol.size;
  }
  const auto count = symbols.size();
  const auto begin_of = [&](size_t index) {
    return index < count ? symbols[index].begin : text.size();
  };
  const auto is_word_symbol = [&](size_t index) {
    return index < count && (IsRussianLower(symbols[index].value) || symbols[index].value == U'+');
  };

  std::vector<StressItem> items;
  std::vector<size_t> group;
  for (size_t i = 0; i < count;) {
    if (!is_word_symbol(i)) {
      const auto symbol = text.substr(begin_of(i), begin_of(i + 1) - begin_of(i));
      items.push_back({false, std::string(symbol), std::string(symbol), std::string(symbol)});
      ++i;
      continue;
    }
    size_t end = i;
    // A hyphen belongs to a word only between its letters.
    while (is_word_symbol(end) || (end < count && symbols[end].value == U'-' && is_word_symbol(end + 1))) {
      ++end;
    }
    const auto word = text.substr(begin_of(i), begin_of(end) - begin_of(i));
    i = end;

    // A hyphenated word may have an override as a whole (with a mark of the user it is not found): its parts are
    // stressed as the override says and read as one word.
    if (
      const auto found = overrides_.find(word); word.find('-') != std::string_view::npos && found != overrides_.end()
    ) {
      const auto written = Split(word, '-');
      const auto stressed = Split(found->second, '-');
      for (size_t part = 0; part < stressed.size(); ++part) {
        if (part > 0) {
          items.push_back({false, "-", "-", "-"});
        }
        items.push_back(
          {true,
           std::string(written[part]),
           WithoutMarks(stressed[part]),
           std::string(stressed[part]),
           StressSource::OVERRIDE,
           part + 1 < stressed.size() ? StressItem::Join::NEXT : StressItem::Join::NONE}
        );
      }
      continue;
    }
    group.clear();
    for (const auto part : Split(word, '-')) {
      if (!group.empty()) {
        items.push_back({false, "-", "-", "-"});
      }
      group.push_back(items.size());
      StressItem item;
      item.is_word = true;
      item.written = WithoutMarks(part);
      item.text = item.written;
      item.stressed = std::string(part);
      items.push_back(std::move(item));
      if (part.find('+') != std::string_view::npos) {
        items.back().source = StressSource::USER;
      }
    }
    if (group.size() > 1) {
      std::vector<StressItem*> parts;
      parts.reserve(group.size());
      for (const auto index : group) {
        parts.push_back(&items[index]);
      }
      MarkHyphenated(parts.data(), parts.size());
    }
  }
  return items;
}

// The parts of a hyphenated word that have no stress of their own become clitics.
void Stresser::MarkHyphenated(StressItem** parts, size_t count) const {
  const auto make_clitic = [](StressItem& part, StressItem::Join join) {
    part.source = StressSource::CLITIC;
    part.join = join;
  };
  const auto is_free = [](const StressItem& part) {
    return part.source == StressSource::NONE;
  };
  // A part before a hyphen that reads with 'ё' ("темно-синий").
  for (size_t i = 0; i + 1 < count; ++i) {
    if (const auto found = yo_first_.find(parts[i]->text); is_free(*parts[i]) && found != yo_first_.end()) {
      parts[i]->text = found->second;
      parts[i]->stressed = found->second;
    }
  }
  const bool by_letters = std::all_of(parts, parts + count, [this](const StressItem* part) {
    return letter_names_.count(part->text) > 0;
  });
  for (size_t i = 0; i < count; ++i) {
    auto& part = *parts[i];
    if (by_letters) {
      // Spelled by letters ("эс-эм-эс"): one word stressed on the last part.
      if (i + 1 < count && is_free(part)) {
        make_clitic(part, StressItem::Join::NEXT);
      }
    } else if (is_free(part) && (i == 0 ? prefixes_ : suffixes_).count(part.text) > 0) {
      make_clitic(part, i == 0 ? StressItem::Join::NEXT : StressItem::Join::PREVIOUS);
    }
  }
  for (size_t i = 0; !by_letters && i + 1 < count; ++i) {
    auto& part = *parts[i];
    const auto& next = *parts[i + 1];
    const bool one_vowel = CountVowels(part.text) == 1;
    // Of two neighbour parts of one vowel only the second is stressed ("из-за").
    if (one_vowel && CountVowels(next.text) == 1 && is_free(part) && is_free(next)) {
      make_clitic(part, StressItem::Join::NEXT);
    }
    const auto last = LetterAt(part.text, LetterCount(part.text) - 1);
    part.compound = is_free(part) && !one_vowel && (last == U'о' || last == U'е')
      && next.source != StressSource::CLITIC;
  }
  // A word (the parts joined to each other) where no part is left to take the stress: its first part with a vowel
  // is stressed as an ordinary word.
  for (size_t begin = 0; begin < count;) {
    size_t end = begin + 1;
    while (end < count
           && (parts[end - 1]->join == StressItem::Join::NEXT || parts[end]->join == StressItem::Join::PREVIOUS)) {
      ++end;
    }
    StressItem* first = nullptr;
    bool has_stress = false;
    for (size_t i = begin; i < end; ++i) {
      if (CountVowels(parts[i]->text) > 0) {
        first = first != nullptr ? first : parts[i];
        has_stress = has_stress || parts[i]->source != StressSource::CLITIC;
      }
    }
    if (first != nullptr && !has_stress) {
      first->source = StressSource::NONE; // it stays joined to its word
    }
    begin = end;
  }
}

bool Stresser::KnowsWord(std::string_view word) const {
  if (dictionary_->Find(word)) {
    return true;
  }
  // The dictionary of 'ё' keeps a word under its spelling with 'е' and marks the letters that are 'ё'.
  const auto marks = yo_ ? yo_->Find(FoldYo(word)) : std::nullopt;
  if (!marks) {
    return false;
  }
  size_t letter = 0;
  for (size_t pos = 0; pos < word.size(); pos += word[pos] == '-' ? 1 : kLetterBytes) {
    if (word.compare(pos, kLetterBytes, "ё") == 0 && !marks->test(letter)) {
      return false;
    }
    ++letter;
  }
  return true;
}

void Stresser::RestoreYo(StressItem& part) const {
  if (part.source != StressSource::NONE || HasYo(part.text) || part.text.find("е") == std::string::npos) {
    return;
  }
  if (const auto extra = yo_extra_.find(part.text); extra != yo_extra_.end()) {
    part.text = extra->second;
  } else if (const auto marks = yo_ ? yo_->Find(part.text) : std::nullopt) {
    for (size_t i = 0; i < LetterCount(part.text); ++i) {
      if (i < marks->size() && marks->test(i) && LetterAt(part.text, i) == U'е') {
        part.text.replace(i * kLetterBytes, kLetterBytes, "ё");
      }
    }
  }
  part.stressed = part.text;
}

// The first part of a compound word ("северо-", "русско-") takes the stress of its stem from the dictionary.
bool Stresser::StressCompound(StressItem& part) const {
  const auto stem = part.text.substr(0, part.text.size() - kLetterBytes);
  for (const char* ending : {"", "ий", "ый", "ой", "а", "я"}) {
    if (auto marks = dictionary_->Find(stem + ending)) {
      for (size_t i = LetterCount(stem); i < marks->size(); ++i) {
        marks->reset(i);
      }
      if (MarksVowel(part.text, *marks)) {
        MarkLetters(part, *marks, StressSource::DICTIONARY);
        return true;
      }
    }
  }
  return false;
}

void Stresser::StressWord(StressItem& part) {
  const auto letters = LetterCount(part.text);
  size_t vowels = 0;
  size_t last_vowel = 0;
  size_t vowel_before_last = 0;
  for (size_t i = 0; i < letters; ++i) {
    if (IsVowel(LetterAt(part.text, i))) {
      ++vowels;
      vowel_before_last = last_vowel;
      last_vowel = i;
    }
  }
  if (vowels == 0) {
    part.source = StressSource::NO_VOWELS;
  } else if (const auto found = overrides_.find(part.text); found != overrides_.end()) {
    SetVariant(part, found->second, StressSource::OVERRIDE);
  } else if (vowels == 1) {
    MarkLetter(part, last_vowel, StressSource::ONE_VOWEL);
  } else if (part.compound && StressCompound(part)) {
    return;
  } else if (const auto marks = dictionary_->Find(part.text); marks && MarksVowel(part.text, *marks)) {
    MarkLetters(part, *marks, StressSource::DICTIONARY);
  } else if (const auto yo = part.text.rfind("ё"); yo != std::string::npos) {
    MarkLetter(part, yo / kLetterBytes, StressSource::YO);
  } else if (model_ && letters <= model_->MaxLetters()) {
    MarkLetter(part, model_->StressedLetter(part.text), StressSource::MODEL);
  } else {
    MarkLetter(part, vowel_before_last, StressSource::FALLBACK);
  }
}

// A homograph is read by a rule of its neighbours that holds; otherwise by the models of the whole sentence when
// they are sure: the model of a marked word, whose reading against the prevailing one of the word
// (homographs_priors.tsv) needs a high probability or the same answer of the model of pairs, then the model of
// pairs; otherwise by the default of the table or the prevailing reading. A word with 'ё' is read as it is written.
void Stresser::ReadHomographs(std::vector<StressItem>& items, const std::vector<StressItem*>& parts) {
  const bool use_model = (homograph_model_ || homograph_classifier_)
    && homograph_mode_ == HomographMode::RULES_THEN_MODEL;
  if (!homographs_ && !(use_model && (homograph_readings_ || homograph_classifier_)) && priors_.empty()) {
    return;
  }
  // The neighbours inside a piece without marks: only spaces and hyphens lie between the words.
  std::vector<std::string> folded(parts.size());
  std::vector<bool> joined(parts.size(), false); // with the previous word
  for (size_t i = 0; i < parts.size(); ++i) {
    folded[i] = FoldYo(parts[i]->text);
    joined[i] = i > 0 && std::all_of(parts[i - 1] + 1, parts[i], [](const StressItem& separator) {
                  return separator.text == " " || separator.text == "-";
                });
  }
  std::vector<size_t> following(parts.size(), 0); // the words after a word up to a mark
  for (size_t i = parts.size(); i-- > 1;) {
    following[i - 1] = joined[i] ? following[i] + 1 : 0;
  }
  const auto by_rules = [&](size_t i) {
    const std::string_view previous = joined[i] ? folded[i - 1] : HomographTable::kNoWordBefore;
    const std::string_view before_previous = joined[i] && joined[i - 1] ? folded[i - 2] : HomographTable::kNoWordBefore;
    const auto after = [&](size_t k) {
      return k < parts.size() && joined[k] ? folded[k] : HomographTable::kNoWordAfter;
    };
    const std::string_view next = after(i + 1);
    const std::string_view after_next = next == HomographTable::kNoWordAfter ? next : after(i + 2);
    return *homographs_->Apply(
      folded[i], previous, next, before_previous, after_next, folded.data() + i + 1, following[i]
    );
  };
  // The models see the sentence as it stood before this stage: a word the stage respells with 'ё' is remembered.
  std::map<const StressItem*, std::string> respelled;
  const auto set = [&](StressItem& part, std::string_view variant, StressSource source) {
    auto before = part.text;
    SetVariant(part, variant, source);
    if (use_model && before != part.text) {
      respelled.emplace(&part, std::move(before));
    }
  };
  const auto set_by_table = [&](StressItem& part, const HomographTable::Choice& choice) {
    set(part, choice.variant, choice.by_rule ? StressSource::HOMOGRAPH_RULE : StressSource::HOMOGRAPH_DEFAULT);
  };

  const auto before_stage = [&](const StressItem& item) -> std::string_view {
    const auto found = respelled.find(&item);
    return found == respelled.end() ? item.text : found->second;
  };
  // The model of a marked word reads the whole text at the first word it is asked of.
  std::optional<std::vector<HomographClassifier::Answer>> answers;
  std::vector<size_t> begins; // of the items in that text
  const auto classified = [&](const StressItem& part) -> const HomographClassifier::Answer* {
    if (!homograph_classifier_->Knows(part.text)) {
      return nullptr;
    }
    if (!answers) {
      std::string text;
      for (const auto& item : items) {
        begins.push_back(text.size());
        text += before_stage(item);
      }
      answers = homograph_classifier_->Read(text);
    }
    const auto begin = begins[static_cast<size_t>(&part - items.data())];
    // The answers stand in the order of the text.
    const auto found = std::lower_bound(answers->begin(), answers->end(), begin, [](const auto& answer, size_t place) {
      return answer.begin < place;
    });
    const bool is_the_word = found != answers->end() && found->begin == begin && found->end == begin + part.text.size();
    return is_the_word ? &*found : nullptr;
  };

  std::vector<HomographModel::Tokens> sentence; // filled at the first word the model of pairs reads
  std::vector<std::string> readings;
  for (size_t i = 0; i < parts.size(); ++i) {
    auto& part = *parts[i];
    if (part.source != StressSource::NONE || HasYo(part.text)) {
      continue;
    }
    const auto* variants = homographs_ ? homographs_->Variants(folded[i]) : nullptr;
    std::optional<HomographTable::Choice> rule;
    if (variants != nullptr) {
      rule = by_rules(i);
      // 'е' against 'ё' of a word of the table is for the table: no model tells them apart.
      if (!use_model || rule->by_rule || std::any_of(variants->begin(), variants->end(), HasYo)) {
        set_by_table(part, *rule);
        continue;
      }
    }
    // The prevailing reading of the word: its default outside the table.
    const auto prior = priors_.find(part.text);
    if (!use_model) {
      if (prior != priors_.end()) {
        SetVariant(part, prior->second.reading, StressSource::HOMOGRAPH_DEFAULT);
      }
      continue;
    }
    // The model of a marked word, then the model of pairs: a word with two readings or more.
    const auto* big = homograph_classifier_ ? classified(part) : nullptr;
    big = big != nullptr && big->probability >= homograph_confidence_ ? big : nullptr;
    readings.clear();
    if (variants != nullptr) {
      readings = variants->size() >= 2 && homograph_model_ ? *variants : readings;
    } else if (const auto marks = homograph_readings_ ? homograph_readings_->Find(part.text) : std::nullopt) {
      for (size_t letter = 0; letter < LetterCount(part.text); ++letter) {
        if (letter < marks->size() && marks->test(letter) && IsVowel(LetterAt(part.text, letter))) {
          readings.push_back(std::string(part.text).insert(letter * kLetterBytes, 1, '+'));
        }
      }
    }
    std::string_view small;
    if (!readings.empty()) {
      if (sentence.empty()) {
        for (const auto& item : items) {
          if (item.text != " ") {
            sentence.push_back(homograph_model_->Encode(before_stage(item)));
          }
        }
      }
      const auto index = std::count_if(items.data(), parts[i], [](const StressItem& item) {
        return item.text != " ";
      });
      const auto choice = homograph_model_->Choose(sentence, static_cast<size_t>(index), readings);
      small = choice.probability >= homograph_confidence_ ? std::string_view(readings[choice.reading]) : small;
    }
    std::string_view chosen = big != nullptr ? big->reading : small;
    // The other reading of a word with a prevailing one needs the sure model or the agreement of the small one,
    // whose readings have no 'ё'.
    const bool is_other = big != nullptr && prior != priors_.end() && big->reading != prior->second.reading;
    if (is_other && big->probability < prior->second.confidence && FoldYo(big->reading) != small) {
      chosen = {};
    }
    if (!chosen.empty()) {
      set(part, chosen, StressSource::HOMOGRAPH_MODEL);
    } else if (rule) {
      set_by_table(part, *rule);
    } else if (prior != priors_.end()) {
      set(part, prior->second.reading, StressSource::HOMOGRAPH_DEFAULT);
    }
  }
}

std::vector<StressItem> Stresser::Stress(std::string_view text) {
  auto items = Parse(text);
  std::vector<StressItem*> parts;
  for (auto& item : items) {
    if (item.is_word) {
      RestoreYo(item);
      parts.push_back(&item);
    }
  }

  ReadHomographs(items, parts);

  for (auto* part : parts) {
    if (part->source == StressSource::NONE) {
      StressWord(*part);
    }
  }
  return items;
}

} // namespace giga_edge::vits
