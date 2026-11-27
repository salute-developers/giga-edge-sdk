#include "giga_edge/modules/tts/interpreter/backends/vits/text/sentence_splitter.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace {

// A place to cut a sentence at: the end of the first piece.
struct Cut {
  size_t offset = 0; // bytes
  bool clause = false;
};

struct Rendered {
  std::string text;
  std::vector<Cut> cuts;
};

bool IsClosing(const Piece& piece) {
  constexpr std::string_view closing_marks = ".,;:?!)";
  return piece.kind != PieceKind::WORD && piece.text.size() == 1
    && closing_marks.find(piece.text.front()) != std::string_view::npos;
}

// The byte after `count` code points from `pos`, stress marks not counted; the end if the text is shorter.
size_t Advance(std::string_view text, size_t pos, size_t count) {
  while (pos < text.size() && count > 0) {
    if (text[pos] != '+') {
      --count;
    }
    ++pos;
    while (!IsUtf8Boundary(text, pos)) {
      ++pos;
    }
  }
  return pos;
}

// The limit of a sentence measured as in the training data of the acoustic model, which has no longer pieces:
// each token, a word or a mark, is its length and 2 (stress marks not counted), a space before a token counts but
// before , : " ). The byte after the last whole token from `pos` within `budget` of that measure; `pos` if the first
// token is longer.
size_t AdvanceByMeasure(std::string_view text, size_t pos, size_t budget) {
  constexpr std::string_view marks_list = ".,;:?!()\"";
  constexpr std::string_view no_space_before = ",:\")";
  constexpr size_t token_reserve = 2;
  size_t total = 0;
  size_t fit = pos;
  bool first = true;
  while (true) {
    while (pos < text.size() && text[pos] == ' ') {
      ++pos;
    }
    if (pos >= text.size()) {
      break;
    }
    const auto start = pos;
    size_t length = 0;
    if (marks_list.find(text[pos]) != std::string_view::npos) {
      ++pos;
      length = 1;
    } else {
      while (pos < text.size() && text[pos] != ' ' && marks_list.find(text[pos]) == std::string_view::npos) {
        length += IsUtf8Boundary(text, pos) && text[pos] != '+' ? 1 : 0;
        ++pos;
      }
    }
    const auto space = !first && no_space_before.find(text[start]) == std::string_view::npos ? 1 : 0;
    if (total + space + length + token_reserve > budget) {
      break;
    }
    total += space + length + token_reserve;
    fit = pos;
    first = false;
  }
  return fit;
}

bool IsRussianVowel(char32_t letter) {
  return std::u32string_view(U"аеёиоуыэюя").find(letter) != std::u32string_view::npos;
}

// The alphabet of the contract, whatever the classes before did: lower-case Russian letters, single spaces, the marks
// .,;:?!-()" and '+' before a vowel. Anything else, invalid UTF-8 too, becomes a space; then spaces are tidied: one
// between words, none before .,;:?!) and after '(', none at the ends.
std::string KeepAlphabet(std::string_view text) {
  std::string kept;
  for (size_t pos = 0; pos < text.size();) {
    CodePoint code_point{0xFFFD, 1};
    try {
      code_point = DecodeUtf8(text, pos);
    } catch (const std::invalid_argument&) {
      code_point = {0xFFFD, 1}; // an invalid byte becomes a space below
    }
    const auto value = code_point.value;
    const auto russian = (value >= U'а' && value <= U'я') || value == U'ё';
    const auto mark = value < 0x80
      && std::string_view(".,;:?!-()\"").find(static_cast<char>(value)) != std::string_view::npos;
    bool stress = false;
    if (value == U'+' && pos + 1 < text.size()) {
      try {
        stress = IsRussianVowel(DecodeUtf8(text, pos + 1).value);
      } catch (const std::invalid_argument&) {
        stress = false; // '+' before an invalid byte is no stress mark
      }
    }
    if (russian || mark || stress) {
      kept.append(text, pos, code_point.size);
    } else {
      kept += ' ';
    }
    pos += code_point.size;
  }
  std::string tidy;
  for (size_t pos = 0; pos < kept.size(); ++pos) {
    const auto letter = kept[pos];
    if (letter == ' ') {
      const auto next = kept.find_first_not_of(' ', pos);
      if (
        tidy.empty() || tidy.back() == '(' || next == std::string::npos
        || std::string_view(".,;:?!)").find(kept[next]) != std::string_view::npos
      ) {
        pos = (next == std::string::npos ? kept.size() : next) - 1;
        continue;
      }
      tidy += ' ';
      pos = next - 1;
      continue;
    }
    // A run of marks, with spaces, quotes or closing brackets between them too, is one mark, the strongest: "а,,, б",
    // "а, - б", "один:.", "слово,\"!" -> "слово\"!".
    // A hyphen joins two letters ("кто-то"), any other '-' is a dash.
    constexpr std::string_view ranks = ",-:;.!?";
    const auto is_letter = [](char byte) {
      return (byte & 0x80) != 0 || byte == '+';
    };
    const auto hyphen = letter == '-' && !tidy.empty() && is_letter(tidy.back()) && pos + 1 < kept.size()
      && is_letter(kept[pos + 1]);
    // A clause mark right after an opening bracket or before a closing one means nothing: "(, —» г" -> "(г",
    // "свет, )" -> "свет)".
    const auto opening = tidy.find_last_not_of(' ');
    if (
      ranks.find(letter) != std::string_view::npos && !hyphen && opening != std::string::npos && tidy[opening] == '('
      && std::string_view(",-:;").find(letter) != std::string_view::npos
    ) {
      continue;
    }
    if (letter == ')') {
      while (!tidy.empty() && std::string_view(" ,-:;").find(tidy.back()) != std::string_view::npos) {
        tidy.pop_back();
      }
    }
    if (ranks.find(letter) != std::string_view::npos && !hyphen) {
      const auto back = tidy.find_last_not_of(" \")");
      // A '-' before a mark joins no letters: a dash ("привет-,").
      const auto dash_back = back != std::string::npos && tidy[back] == '-';
      if (
        back != std::string::npos && ranks.find(tidy[back]) != std::string_view::npos
        && (tidy[back] != '-' || dash_back)
      ) {
        if (ranks.find(letter) > ranks.find(tidy[back])) {
          tidy.erase(back, 1);
          while (!tidy.empty() && tidy.back() == ' ') {
            tidy.pop_back();
          }
          tidy += letter == '-' ? " -" : std::string(1, letter);
        }
        continue;
      }
    }
    tidy += letter;
  }
  // Spaces left by a run of marks: one between words, none before a mark.
  std::string single;
  for (size_t pos = 0; pos < tidy.size(); ++pos) {
    // The rules of the contract once more, after the runs: no space at the ends, after '(', before a closing mark.
    if (
      tidy[pos] == ' '
      && (single.empty() || single.back() == '(' || pos + 1 >= tidy.size() || tidy[pos + 1] == ' '
          || std::string_view(".,;:?!)").find(tidy[pos + 1]) != std::string_view::npos)
    ) {
      continue;
    }
    // A dash left after a letter of a run is a dash between words: "привет- мир" -> "привет - мир".
    const auto glued_dash = tidy[pos] == '-' && pos > 0 && ((tidy[pos - 1] & 0x80) != 0 || tidy[pos - 1] == '+')
      && (pos + 1 >= tidy.size() || tidy[pos + 1] == ' ');
    if (glued_dash) {
      single += ' ';
    }
    single += tidy[pos];
  }
  return single;
}

bool HasLetters(std::string_view text) {
  for (size_t pos = 0; pos < text.size();) {
    const auto code_point = DecodeUtf8(text, pos);
    if (IsLetter(code_point.value)) {
      return true;
    }
    pos += code_point.size;
  }
  return false;
}

// Words with one space between them, closing marks right after the word before, nothing after '('. A group of words
// is cut inside only if it is longer than a sentence.
Rendered Render(const std::vector<Piece>& pieces, size_t begin, size_t end, size_t max_chars) {
  Rendered rendered;
  bool last_word = false;
  bool after_opening = false;
  bool after_closing = false;
  bool after_dash = false;
  bool after_clause = false;
  for (size_t index = begin; index < end; ++index) {
    const auto& piece = pieces[index];
    if (piece.text.empty()) {
      continue;
    }
    // A word after a word or after a closing mark always has a space before it: "играет queen, null" as written
    // "играет Queen,null".
    // A dash between words always has spaces around it: "свет - включен".
    const auto dash = piece.kind == PieceKind::CLAUSE && piece.text == "-";
    const auto space = !rendered.text.empty() && !IsClosing(piece) && !after_opening
      && (piece.space_before || dash || after_dash || (piece.kind == PieceKind::WORD && (last_word || after_closing)));
    if (!rendered.text.empty() && (after_clause || space)) {
      rendered.cuts.push_back({rendered.text.size(), after_clause});
    }
    if (space) {
      rendered.text += ' ';
    }
    const auto start = rendered.text.size();
    rendered.text += piece.text;
    if (piece.kind == PieceKind::WORD && AdvanceByMeasure(piece.text, 0, max_chars) < piece.text.size()) {
      for (auto space_pos = piece.text.find(' '); space_pos != std::string::npos;
           space_pos = piece.text.find(' ', space_pos + 1)) {
        rendered.cuts.push_back({start + space_pos, false});
      }
    }
    last_word = piece.kind == PieceKind::WORD;
    after_opening = piece.text == "(";
    after_closing = IsClosing(piece);
    after_dash = dash;
    after_clause = piece.kind == PieceKind::CLAUSE;
  }
  return rendered;
}

class Splitter {
public:
  explicit Splitter(const SentenceParams& params)
    : params_(params) {}

  // `end` is the end mark with the closing quotes after it: "?\"".
  void AddSentence(const Rendered& rendered, std::string end) {
    const std::string_view text = rendered.text;
    const auto max_chars = static_cast<size_t>(params_.max_sentence_chars);
    // The end marks are within the limit too; a long run of closing marks is not spoken: "я?)))…" keeps "?)))".
    constexpr size_t max_closing = 3;
    if (const auto closing = end.find_first_of("\")"); closing != std::string::npos) {
      end.resize(std::min(end.size(), closing + max_closing));
    }
    // The end marks by the measure: a space before the end mark, none before the closing ones.
    size_t end_cost = 0;
    for (const char mark : end) {
      end_cost += 1 + 2 + (mark == '"' || mark == ')' ? 0 : 1);
    }
    first_of_sentence_ = sentences_.size();
    size_t pos = 0;
    size_t first_cut = 0;
    while (true) {
      while (pos < text.size() && text[pos] == ' ') {
        ++pos;
      }
      if (pos >= text.size()) {
        // The text ended at a cut: the last piece takes the end mark and the pause.
        Add({}, end, params_.sentence_pause_ms, true);
        return;
      }
      if (AdvanceByMeasure(text, pos, max_chars - std::min(end_cost, max_chars)) >= text.size()) {
        Add(text.substr(pos), end, params_.sentence_pause_ms, true);
        return;
      }
      // The dot of a cut piece is not measured.
      auto limit = AdvanceByMeasure(text, pos, max_chars);
      if (limit == pos) {
        // A word longer than a sentence: its letters and the 2 of the measure.
        limit = Advance(text, pos, max_chars - 2);
      }
      // The last cut at ;:,— within the limit, else the last one at a space.
      const Cut* best = nullptr;
      while (first_cut < rendered.cuts.size() && rendered.cuts[first_cut].offset <= pos) {
        ++first_cut;
      }
      for (auto index = first_cut; index < rendered.cuts.size() && rendered.cuts[index].offset <= limit; ++index) {
        const auto& cut = rendered.cuts[index];
        if (best == nullptr || cut.clause || !best->clause) {
          best = &cut;
        }
      }
      // Only the end mark is over the limit and no clause mark to cut at: the whole text, as the training data has
      // such marks in a piece of their own.
      if ((best == nullptr || !best->clause) && limit >= text.size()) {
        Add(text.substr(pos), end, params_.sentence_pause_ms, true);
        return;
      }
      // No place to cut: a word longer than a sentence is cut at the limit, which never parts '+' from its vowel.
      const auto cut_at = best != nullptr ? best->offset : limit;
      const auto clause = best != nullptr && best->clause;
      Add(text.substr(pos, cut_at - pos), ".", clause ? params_.clause_pause_ms : params_.word_pause_ms, false);
      pos = cut_at;
    }
  }

  std::vector<NormalizedSentence> Take() {
    if (!sentences_.empty()) {
      sentences_.back().pause_after_ms = 0;
    }
    return std::move(sentences_);
  }

private:
  // Without the marks of a cut at the ends: "- спросил он", "свет, ". A last piece without letters gives its end mark
  // and its pause to the piece before it of the same sentence.
  void Add(std::string_view piece, const std::string& end, int pause_ms, bool final) {
    auto text = KeepAlphabet(piece);
    // Nor hanging brackets and colons: "свет)", "(свет", ": свет".
    const auto first = text.find_first_not_of(" ,-:).;!?");
    const auto last = text.find_last_not_of(" ,;:-(.!?");
    if (first == std::string::npos || !HasLetters(text)) {
      if (final && sentences_.size() > first_of_sentence_) {
        auto& before = sentences_.back();
        before.text.back() = end.front();
        before.pause_after_ms = pause_ms;
      }
      return;
    }
    // Nor a mark before the closing quotes at the end, the end mark goes after them: "шесть:\"." -> "шесть\".".
    auto body = text.substr(first, last + 1 - first);
    for (auto at = body.find_last_not_of("\")"); at != std::string::npos && at + 1 < body.size()
         && std::string_view(",;:-.!?").find(body[at]) != std::string_view::npos;
         at = body.find_last_not_of("\")")) {
      body.erase(at, 1);
    }
    sentences_.push_back({body + end, pause_ms});
  }

  const SentenceParams& params_;
  std::vector<NormalizedSentence> sentences_;
  size_t first_of_sentence_ = 0;
};

} // namespace

void CheckSentenceParams(const SentenceParams& params) {
  if (params.max_sentence_chars < kMinSentenceChars || params.max_sentence_chars > kMaxSentenceChars) {
    throw std::invalid_argument(
      "max_sentence_chars must be in [" + std::to_string(kMinSentenceChars) + ", " + std::to_string(kMaxSentenceChars)
      + "], got " + std::to_string(params.max_sentence_chars)
    );
  }
  for (const auto pause : {params.sentence_pause_ms, params.clause_pause_ms, params.word_pause_ms}) {
    if (pause < 0 || pause > kMaxPauseMs) {
      throw std::invalid_argument(
        "pauses must be in [0, " + std::to_string(kMaxPauseMs) + "] ms, got " + std::to_string(pause)
      );
    }
  }
}

std::vector<NormalizedSentence> SplitSentences(const std::vector<Piece>& pieces, const SentenceParams& params) {
  CheckSentenceParams(params);
  const auto max_chars = static_cast<size_t>(params.max_sentence_chars);
  Splitter splitter(params);
  size_t begin = 0;
  for (size_t index = 0; index < pieces.size(); ++index) {
    if (pieces[index].kind != PieceKind::END) {
      continue;
    }
    // Closing quotes right after the end mark go with the sentence: "\"ты придёшь?\"".
    std::string end = pieces[index].text;
    auto next = index + 1;
    while (next < pieces.size() && pieces[next].kind == PieceKind::MARK && !pieces[next].space_before
           && (pieces[next].text == "\"" || pieces[next].text == ")")) {
      end += pieces[next].text;
      ++next;
    }
    splitter.AddSentence(Render(pieces, begin, index, max_chars), end);
    begin = next;
    index = next - 1;
  }
  splitter.AddSentence(Render(pieces, begin, pieces.size(), max_chars), ".");
  return splitter.Take();
}

} // namespace giga_edge::vits
