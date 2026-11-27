#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress.h"
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

constexpr char32_t kPause = U'^';

// What the stage may write to the acoustic input at all, whatever else the voice knows.
bool IsContractSymbol(char32_t symbol) {
  return IsRussianLower(symbol)
    || std::u32string_view(U"АЕЁИОУЫЭЮЯ .,;:?!-()\"^").find(symbol) != std::u32string_view::npos;
}

enum class TokenKind { WORD, MARK, SPACE, PAUSE };

struct Token {
  TokenKind kind = TokenKind::MARK;
  std::string text;
  size_t item = 0; // of a word
};

bool IsMark(const Token& token, std::string_view marks) {
  return token.kind == TokenKind::MARK && token.text.size() == 1 && marks.find(token.text[0]) != std::string_view::npos;
}

char32_t ToUpper(char32_t letter) {
  return letter == U'ё' ? U'Ё' : letter - (U'а' - U'А');
}

// '^' before ',', ':', ';', before a dash between spaces and before a ')' that words follow, after '.', '?', '!'
// inside the sentence.
std::vector<Token> AddPauses(const std::vector<Token>& tokens) {
  const Token pause{TokenKind::PAUSE, "^"};
  const auto count = tokens.size();
  size_t last_word = 0;
  for (size_t i = 0; i < count; ++i) {
    last_word = tokens[i].kind == TokenKind::WORD ? i : last_word;
  }
  std::vector<Token> result;
  result.reserve(count + 8);
  const auto add_pause = [&result, &pause]() {
    if (result.empty() || result.back().kind != TokenKind::PAUSE) {
      result.push_back(pause);
    }
  };
  for (size_t i = 0; i < count; ++i) {
    const auto& token = tokens[i];
    const bool dash = IsMark(token, "-") && i > 0 && i + 1 < count && tokens[i - 1].kind == TokenKind::SPACE
      && tokens[i + 1].kind == TokenKind::SPACE;
    if (IsMark(token, ",:;") || (IsMark(token, ")") && i < last_word)) {
      add_pause();
      result.push_back(token);
    } else if (dash) {
      const auto space = std::move(result.back());
      result.pop_back();
      add_pause();
      result.push_back(space);
      result.push_back(token);
    } else {
      result.push_back(token);
      if (IsMark(token, ".?!") && i + 1 < count && !IsMark(tokens[i + 1], ".?!")) {
        result.push_back(pause);
      }
    }
  }
  return result;
}

// A comma right before a no-pause phrase or right after it takes no pause; each side is looked at on its own.
void DropPausesAtPhrases(
  std::vector<Token>& tokens, const std::vector<StressItem>& items, const std::vector<std::vector<std::string>>& phrases
) {
  const auto is_comma_pause = [&tokens](size_t index) {
    return tokens[index].kind == TokenKind::PAUSE && index + 1 < tokens.size() && IsMark(tokens[index + 1], ",");
  };
  std::vector<size_t> words;
  bool has_comma_pause = false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind == TokenKind::WORD) {
      words.push_back(i);
    }
    has_comma_pause = has_comma_pause || is_comma_pause(i);
  }
  if (!has_comma_pause) {
    return;
  }
  std::vector<std::string> folded;
  folded.reserve(words.size());
  for (const auto index : words) {
    folded.push_back(FoldYo(items[tokens[index].item].text));
  }
  const auto is_inside = [](const Token& token) {
    return token.kind == TokenKind::WORD || token.kind == TokenKind::SPACE || IsMark(token, "-");
  };
  // The words of a phrase are whole words: no hyphen glues them to a part of a hyphenated word.
  const auto is_glued = [&tokens](size_t first, size_t last) {
    return (first > 0 && IsMark(tokens[first - 1], "-")) || (last + 1 < tokens.size() && IsMark(tokens[last + 1], "-"));
  };

  std::vector<bool> drop(tokens.size(), false);
  for (size_t word = 0; word < words.size(); ++word) {
    for (const auto& phrase : phrases) {
      if (phrase.size() > words.size() - word || !std::equal(phrase.begin(), phrase.end(), folded.begin() + word)) {
        continue;
      }
      const auto first = words[word];
      const auto last = words[word + phrase.size() - 1];
      if (!std::all_of(tokens.begin() + first, tokens.begin() + last + 1, is_inside) || is_glued(first, last)) {
        continue;
      }
      for (const int step : {-1, 1}) {
        for (auto i = static_cast<ptrdiff_t>(step < 0 ? first : last) + step;
             i >= 0 && i < static_cast<ptrdiff_t>(tokens.size()) && tokens[i].kind != TokenKind::WORD;
             i += step) {
          if (tokens[i].kind == TokenKind::PAUSE) {
            drop[i] = is_comma_pause(i);
            break;
          }
        }
      }
      break;
    }
  }
  std::vector<Token> kept;
  kept.reserve(tokens.size());
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (!drop[i]) {
      kept.push_back(std::move(tokens[i]));
    }
  }
  tokens = std::move(kept);
}

} // namespace

PreparedSentence Stresser::Prepare(
  const std::vector<StressItem>& items, std::u32string_view alphabet, std::string* dropped
) const {
  if (alphabet.find(U' ') == std::u32string_view::npos) {
    throw std::invalid_argument("vits: the voice alphabet has no space");
  }
  const auto is_known = [alphabet](char32_t symbol) {
    return IsContractSymbol(symbol) && alphabet.find(symbol) != std::u32string_view::npos;
  };
  const auto drop = [dropped](char32_t symbol) {
    if (dropped != nullptr) {
      AppendUtf8(*dropped, symbol);
    }
  };
  // The letters of a word: an upper-case vowel after a '+' and for every 'ё'; 'э' for 'е' where the table says so.
  const auto acoustic_word = [&](const StressItem& item) {
    const auto found = e_to_eh_.find(item.text);
    const std::string_view respelled = found != e_to_eh_.end() ? found->second : std::string_view();
    std::string word;
    bool stress = false;
    size_t letter = 0;
    for (size_t pos = 0; pos < item.stressed.size();) {
      auto symbol = DecodeUtf8(item.stressed, pos);
      pos += symbol.size;
      if (symbol.value == U'+') {
        stress = true;
        continue;
      }
      if (!respelled.empty()) {
        symbol.value = LetterAt(respelled, letter);
      }
      ++letter;
      const bool upper = stress || symbol.value == U'ё'; // a '+' stands before a vowel only
      stress = false;
      if (upper && is_known(ToUpper(symbol.value))) {
        AppendUtf8(word, ToUpper(symbol.value));
      } else if (is_known(symbol.value)) {
        AppendUtf8(word, symbol.value);
        if (upper) {
          drop(ToUpper(symbol.value));
        }
      } else {
        drop(symbol.value);
      }
    }
    return word;
  };

  std::vector<Token> spaced;
  spaced.reserve(items.size());
  bool quote_open = false;
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.is_word) {
      spaced.push_back({TokenKind::WORD, acoustic_word(item), i});
      continue;
    }
    // A separator outside the alphabet still separates the words: it reads as a space. So does a '^' of the text:
    // the pauses are the stage's own.
    const bool space = item.text == " ";
    const auto symbol = DecodeUtf8(item.text, 0).value;
    if (!space && (symbol == kPause || !is_known(symbol))) {
      drop(symbol);
    } else if (!space) {
      Token mark{TokenKind::MARK, item.text};
      if (IsMark(mark, ".,:;?!)") || (IsMark(mark, "\"") && quote_open)) {
        while (!spaced.empty() && spaced.back().kind == TokenKind::SPACE) {
          spaced.pop_back();
        }
      }
      quote_open = quote_open != IsMark(mark, "\"");
      spaced.push_back(std::move(mark));
      continue;
    }
    if (!spaced.empty() && spaced.back().kind != TokenKind::SPACE) {
      spaced.push_back({TokenKind::SPACE, " "});
    }
  }

  // No space after an opening bracket or quote, none at the edges.
  std::vector<Token> tokens;
  tokens.reserve(spaced.size());
  quote_open = false;
  for (auto& token : spaced) {
    if (
      token.kind == TokenKind::SPACE && !tokens.empty()
      && (IsMark(tokens.back(), "(") || (IsMark(tokens.back(), "\"") && quote_open))
    ) {
      continue;
    }
    quote_open = quote_open != IsMark(token, "\"");
    tokens.push_back(std::move(token));
  }
  while (!tokens.empty() && tokens.back().kind == TokenKind::SPACE) {
    tokens.pop_back();
  }

  PreparedSentence sentence;
  std::vector<TextRange> plain_ranges(items.size());
  for (const auto& token : tokens) {
    if (token.kind == TokenKind::WORD) {
      plain_ranges[token.item] = {sentence.plain.size(), items[token.item].written.size()};
      sentence.plain += items[token.item].written;
    } else {
      sentence.plain += token.text;
    }
  }

  if (is_known(kPause)) {
    tokens = AddPauses(tokens);
    DropPausesAtPhrases(tokens, items, no_pause_);
  }

  std::vector<TextRange> marked_ranges(items.size());
  for (const auto& token : tokens) {
    if (token.kind == TokenKind::WORD) {
      marked_ranges[token.item] = {sentence.marked.size(), token.text.size()};
    }
    sentence.marked += token.text;
  }
  auto previous_join = StressItem::Join::NONE;
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].is_word) {
      continue;
    }
    if (
      !sentence.words.empty()
      && (previous_join == StressItem::Join::NEXT || items[i].join == StressItem::Join::PREVIOUS)
    ) {
      auto& word = sentence.words.back();
      word.plain.size = plain_ranges[i].begin + plain_ranges[i].size - word.plain.begin;
      word.marked.size = marked_ranges[i].begin + marked_ranges[i].size - word.marked.begin;
    } else {
      sentence.words.push_back({plain_ranges[i], marked_ranges[i]});
    }
    previous_join = items[i].join;
  }
  return sentence;
}

} // namespace giga_edge::vits
