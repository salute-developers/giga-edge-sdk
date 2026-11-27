#include "giga_edge/modules/tts/interpreter/backends/vits/text/homograph_classifier.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace giga_edge::vits {

namespace {

constexpr size_t kContextSymbols = 150;
constexpr size_t kMaxWordLength = 100;
// [CLS], two contexts, the markers, a word of the readings and [SEP] at the least.
constexpr size_t kLeastPositions = 2 * kContextSymbols + kMaxWordLength + 4;

bool IsRussianUpper(char32_t ch) {
  return (ch >= U'А' && ch <= U'Я') || ch == U'Ё';
}

bool IsLatin(char32_t ch) {
  return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z');
}

// The white space of the model the text was cleaned for.
bool IsSpace(char32_t ch) {
  return (ch >= 0x09 && ch <= 0x0D) || (ch >= 0x1C && ch <= 0x20) || ch == 0x85 || ch == 0xA0 || ch == 0x1680
    || (ch >= 0x2000 && ch <= 0x200A) || ch == 0x2028 || ch == 0x2029 || ch == 0x202F || ch == 0x205F || ch == 0x3000;
}

bool IsStop(char32_t ch) {
  return ch == U'.' || ch == U'!' || ch == U'?';
}

char32_t Lower(char32_t ch) {
  if (ch == U'Ё') {
    return U'ё';
  }
  return (ch >= U'А' && ch <= U'Я') || (ch >= U'A' && ch <= U'Z') ? ch + 0x20 : ch;
}

char32_t Upper(char32_t ch) {
  if (ch == U'ё') {
    return U'Ё';
  }
  return (ch >= U'а' && ch <= U'я') || (ch >= U'a' && ch <= U'z') ? ch - 0x20 : ch;
}

std::string ToUtf8(std::u32string_view symbols) {
  std::string text;
  for (const auto ch : symbols) {
    AppendUtf8(text, ch);
  }
  return text;
}

// The text cleaned as the contexts of the model were. Step by step: only letters, digits, white space and ".!?,-"
// stay; white space is one space; hyphens in a row are " - "; the same ".", "!" or "?" in a row is one, so are
// commas; no space before ".,!?" (and the repeats this makes are one again); a space after ".,!?" before another
// symbol; spaces are single, none at the ends. The case stays. Every step reads the text from left to right and
// looks back only, so the cleaned text before a word and after it is the cleaned context of the word but for the
// spaces next to the word.
struct Cleaned {
  static constexpr size_t kAdded = static_cast<size_t>(-1);

  std::u32string symbols;
  std::vector<size_t> sources; // of a symbol: its place in the text, kAdded for a symbol the cleaning added

  void Add(char32_t symbol, size_t source) {
    symbols += symbol;
    sources.push_back(source);
  }

  void Pop() {
    symbols.pop_back();
    sources.pop_back();
  }
};

bool IsStopOrComma(char32_t ch) {
  return IsStop(ch) || ch == U',';
}

Cleaned WithoutRepeats(const Cleaned& source) {
  Cleaned result;
  for (size_t i = 0; i < source.symbols.size(); ++i) {
    const auto ch = source.symbols[i];
    if (!(IsStopOrComma(ch) && !result.symbols.empty() && result.symbols.back() == ch)) {
      result.Add(ch, source.sources[i]);
    }
  }
  return result;
}

Cleaned Clean(std::u32string_view text) {
  Cleaned kept;
  for (size_t i = 0; i < text.size(); ++i) {
    const auto ch = text[i];
    if (IsSpace(ch)) {
      if (kept.symbols.empty() || kept.symbols.back() != U' ') {
        kept.Add(U' ', Cleaned::kAdded);
      }
    } else if (
      IsLatin(ch) || IsRussianLower(ch) || IsRussianUpper(ch) || (ch >= U'0' && ch <= U'9') || IsStopOrComma(ch)
      || ch == U'-'
    ) {
      kept.Add(ch, i);
    }
  }
  Cleaned spaced;
  for (size_t i = 0; i < kept.symbols.size();) {
    size_t end = i;
    while (end < kept.symbols.size() && kept.symbols[end] == U'-') {
      ++end;
    }
    if (end - i >= 2) {
      for (const auto ch : std::u32string_view(U" - ")) {
        spaced.Add(ch, Cleaned::kAdded);
      }
      i = end;
    } else {
      spaced.Add(kept.symbols[i], kept.sources[i]);
      ++i;
    }
  }
  spaced = WithoutRepeats(spaced);
  Cleaned tight;
  for (size_t i = 0; i < spaced.symbols.size(); ++i) {
    if (IsStopOrComma(spaced.symbols[i])) {
      while (!tight.symbols.empty() && tight.symbols.back() == U' ') {
        tight.Pop();
      }
    }
    tight.Add(spaced.symbols[i], spaced.sources[i]);
  }
  tight = WithoutRepeats(tight);
  Cleaned result;
  for (size_t i = 0; i < tight.symbols.size(); ++i) {
    const auto ch = tight.symbols[i];
    if (ch == U' ') {
      if (!result.symbols.empty() && result.symbols.back() != U' ') {
        result.Add(ch, Cleaned::kAdded);
      }
      continue;
    }
    result.Add(ch, tight.sources[i]);
    if (IsStopOrComma(ch) && i + 1 < tight.symbols.size() && tight.symbols[i + 1] != U' ') {
      result.Add(U' ', Cleaned::kAdded);
    }
  }
  while (!result.symbols.empty() && result.symbols.back() == U' ') {
    result.Pop();
  }
  return result;
}

} // namespace

HomographClassifier::HomographClassifier(
  const TextModel& model,
  const std::string& vocabulary_path,
  const std::string& readings_path,
  const IDataReaderPtr& data_reader
) {
  constexpr auto int64 = Tensor::DataType::INT64;
  CheckTextModel(
    model,
    {{"input_ids", int64, 2}, {"homo_start_ids", int64, 1}, {"homo_end_ids", int64, 1}},
    {{"logits", Tensor::DataType::FLOAT32, 2}}
  );
  model_ = model;

  auto vocabulary = ToString(EnsureNotNull(data_reader->ReadTransientData(vocabulary_path), vocabulary_path));
  TakePositions(vocabulary, vocabulary_path, kLeastPositions);
  try {
    tokenizer_.emplace(vocabulary, kMaxWordLength);
  } catch (const std::exception& error) {
    throw std::runtime_error("vits: homograph model vocabulary '" + vocabulary_path + "': " + error.what());
  }
  const auto id = [&](const char* token) {
    const auto found = tokenizer_->Find(token);
    if (!found) {
      throw std::runtime_error("vits: homograph model vocabulary '" + vocabulary_path + "' has no " + token);
    }
    return *found;
  };
  pad_ = id("[PAD]");
  cls_ = id("[CLS]");
  sep_ = id("[SEP]");
  open_ = id("[HOMO]");
  close_ = id("[/HOMO]");

  const auto is_reading = [](std::string_view reading, std::string_view word) {
    return IsRussianWord(word) && IsStressedWord(reading) && std::count(reading.begin(), reading.end(), '+') == 1
      && FoldYo(WithoutMarks(reading)) == FoldYo(word);
  };
  const auto readings = ToString(EnsureNotNull(data_reader->ReadTransientData(readings_path), readings_path));
  for (const auto& row : ReadTable(readings)) {
    if (
      row.fields.size() != 3 || !is_reading(row.fields[1], row.fields[0]) || !is_reading(row.fields[2], row.fields[0])
      || row.fields[1] == row.fields[2]
    ) {
      ThrowTableError(readings_path, row.line, "expected a word and its two readings");
    }
    std::array<std::string, 2> pair;
    pair[0] = std::string(row.fields[1]);
    pair[1] = std::string(row.fields[2]);
    if (!readings_.emplace(std::string(row.fields[0]), std::move(pair)).second) {
      ThrowTableError(readings_path, row.line, "the word '" + std::string(row.fields[0]) + "' is repeated");
    }
  }
}

bool HomographClassifier::Knows(std::string_view word) const {
  return readings_.find(word) != readings_.end();
}

void HomographClassifier::Append(std::vector<int64_t>& row, std::string_view text) const {
  for (const auto& token : tokenizer_->Tokenize(text)) {
    row.push_back(token.id);
  }
}

std::vector<HomographClassifier::Answer> HomographClassifier::Read(std::string_view text) {
  std::u32string symbols;
  std::vector<size_t> offsets;
  for (size_t pos = 0; pos < text.size();) {
    const auto symbol = DecodeUtf8(text, pos);
    symbols += symbol.value;
    offsets.push_back(pos);
    pos += symbol.size;
  }
  offsets.push_back(text.size());
  const auto is_word_symbol = [&symbols](size_t i) {
    return IsRussianLower(symbols[i]) || IsRussianUpper(symbols[i]) || symbols[i] == U'+';
  };

  // The cleaned text and the places of the symbols of the text in it.
  const auto cleaned = Clean(symbols);
  std::vector<size_t> places(symbols.size(), 0);
  for (size_t i = 0; i < cleaned.sources.size(); ++i) {
    if (cleaned.sources[i] != Cleaned::kAdded) {
      places[cleaned.sources[i]] = i;
    }
  }
  const std::u32string_view clean = cleaned.symbols;
  // A left context loses the marks at its start and begins with a capital letter there.
  const auto start = std::min(clean.find_first_not_of(U" .,!?-"), clean.size());

  std::vector<Answer> answers;
  std::vector<std::vector<int64_t>> rows;
  std::vector<const std::array<std::string, 2>*> choices;
  size_t length = 0;
  for (size_t begin = 0; begin < symbols.size();) {
    if (!is_word_symbol(begin)) {
      ++begin;
      continue;
    }
    size_t end = begin;
    std::u32string lower;
    for (; end < symbols.size() && is_word_symbol(end); ++end) {
      lower += Lower(symbols[end]);
    }
    const auto word = ToUtf8(lower);
    const auto readings = readings_.find(word);
    if (readings != readings_.end()) {
      // The 150 symbols before the word, the rest in lower case.
      size_t left_end = places[begin];
      left_end -= left_end > 0 && clean[left_end - 1] == U' ' ? 1 : 0;
      std::u32string left;
      for (size_t i = std::max(start, left_end - std::min(left_end, kContextSymbols)); i < left_end; ++i) {
        left += i == start ? Upper(clean[i]) : Lower(clean[i]);
      }
      // The 150 symbols after it: the first as it is, the rest in lower case, a full stop at the end of the text
      // that has none.
      size_t right_begin = places[end - 1] + 1;
      right_begin += right_begin < clean.size() && clean[right_begin] == U' ' ? 1 : 0;
      std::u32string right;
      for (size_t i = right_begin; i < clean.size() && right.size() < kContextSymbols; ++i) {
        right += i == right_begin ? clean[i] : Lower(clean[i]);
      }
      if (!right.empty() && right.size() < kContextSymbols && !IsStop(clean.back())) {
        right += U'.';
      }
      std::vector<int64_t> row{cls_};
      Append(row, ToUtf8(left));
      row.push_back(open_);
      Append(row, word);
      row.push_back(close_);
      Append(row, ToUtf8(right));
      row.push_back(sep_);
      length = std::max(length, row.size());
      rows.push_back(std::move(row));
      choices.push_back(&readings->second);
      answers.push_back({offsets[begin], offsets[end], {}, 0.0});
    }
    begin = end;
  }
  if (rows.empty()) {
    return answers;
  }

  const auto batch = TensorShape({rows.size(), length});
  model_.session->ResizeInput("input_ids", batch.data(), 2);
  model_.session->ResizeInput("homo_start_ids", batch.data(), 1);
  model_.session->ResizeInput("homo_end_ids", batch.data(), 1);
  auto* ids = model_.session->Input("input_ids")->Int64Data();
  auto* starts = model_.session->Input("homo_start_ids")->Int64Data();
  auto* ends = model_.session->Input("homo_end_ids")->Int64Data();
  for (const auto& row : rows) {
    ids = std::fill_n(std::copy(row.begin(), row.end(), ids), length - row.size(), pad_);
    *starts++ = std::find(row.begin(), row.end(), open_) - row.begin();
    *ends++ = std::find(row.begin(), row.end(), close_) - row.begin();
  }
  RunTextModel(model_);

  const auto output = model_.session->Output("logits");
  if (output->GetShape() != std::vector<int>{batch[0], 1}) {
    ThrowTextModelError(model_, "the output 'logits' is not [rows, 1]");
  }
  const float* logits = output->FloatData();
  for (size_t i = 0; i < rows.size(); ++i) {
    const double logit = logits[i];
    if (!std::isfinite(logit)) {
      ThrowTextModelError(model_, "the output 'logits' is not a number");
    }
    answers[i].reading = (*choices[i])[logit > 0 ? 1 : 0];
    answers[i].probability = 1.0 / (1.0 + std::exp(-std::abs(logit)));
  }
  return answers;
}

} // namespace giga_edge::vits
