#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalizer.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/text/morph_dict.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalization.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace normalization {

std::vector<Token> Tokenize(std::string_view text) {
  std::vector<Token> tokens;
  bool space = false;
  bool line_start = true;
  size_t pos = 0;
  while (pos < text.size()) {
    const auto code_point = Decode(text, pos);
    if (!code_point) {
      ++pos;
      continue;
    }
    if (IsSpace(code_point->value)) {
      space = true;
      line_start = line_start || code_point->value == U'\n';
      pos += code_point->size;
      continue;
    }
    // A terminal escape sequence: "\x1b[31m".
    constexpr char32_t escape_value = 0x1B;
    if (code_point->value == escape_value && pos + 1 < text.size() && text[pos + 1] == '[') {
      pos += 2;
      while (pos < text.size() && !(text[pos] >= '@' && text[pos] <= '~')) {
        ++pos;
      }
      ++pos;
      continue;
    }
    Token token;
    token.space_before = space;
    token.line_start = line_start;
    space = false;
    line_start = false;
    const auto begin = pos;
    if (code_point->value >= U'0' && code_point->value <= U'9') {
      token.type = TokenType::NUMBER;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        ++pos;
      }
    } else if (IsWordAt(text, pos)) {
      token.type = TokenType::WORD;
      constexpr char32_t acute = 0x301;
      while (pos < text.size()) {
        const auto letter = Decode(text, pos);
        if (letter && letter->value == acute) {
          // "за́мок" -> "з+амок": the mark goes before its vowel.
          const auto vowel = token.lower.size() >= 2 ? token.lower.size() - 2 : 0;
          const auto last = Decode(token.lower, vowel);
          if (last && last->size == 2 && IsCyrillicVowel(last->value)) {
            token.lower.insert(vowel, "+");
            pos += letter->size;
            continue;
          }
        }
        // "Beyonce\u0301": a combining mark stays after a Latin letter; elsewhere it is left out ("зам\u0301ок").
        if (letter && IsCombiningMark(letter->value)) {
          if (!token.lower.empty() && IsLatinLetter(static_cast<unsigned char>(token.lower.back()))) {
            token.lower.append(text, pos, letter->size);
          }
          pos += letter->size;
          continue;
        }
        if (!IsWordAt(text, pos)) {
          break;
        }
        AppendUtf8(token.lower, ToLower(letter->value));
        pos += letter->size;
      }
    } else {
      pos += code_point->size;
    }
    token.text = std::string(text.substr(begin, pos - begin));
    tokens.push_back(std::move(token));
  }
  return tokens;
}

void Normalization::AgreePreposition(size_t first_piece) {
  if (first_piece == 0 || first_piece >= pieces_.size() || pieces_[first_piece - 1].kind != PieceKind::WORD) {
    return;
  }
  auto& preposition = pieces_[first_piece - 1].text;
  const auto word = LookupKey(pieces_[first_piece].text);
  const auto starts = [&word](std::string_view prefix) {
    return word.compare(0, prefix.size(), prefix) == 0;
  };
  const auto cluster = starts("ст") || starts("вт") || starts("вс") || starts("мн");
  if (preposition == "с" && cluster) {
    preposition = "со";
  } else if ((preposition == "в" || preposition == "к") && (starts("вт") || starts("вс") || starts("мн"))) {
    preposition += "о";
  } else if (preposition == "о" && (starts("о") || starts("а") || starts("у") || starts("и") || starts("э"))) {
    preposition = "об";
  }
}

std::vector<Piece> Normalization::Run() {
  size_t index = 0;
  while (index < tokens_.size()) {
    const auto first_piece = pieces_.size();
    if (const auto end = TryNumber(index)) {
      index = *end;
      AgreePreposition(first_piece);
    } else if (tokens_[index].type == TokenType::WORD) {
      const auto roman = TryRoman(index);
      if (roman) {
        AgreePreposition(first_piece);
      }
      index = roman ? *roman : AddWord(index);
    } else if (IsSentenceEnd(index)) {
      index = AddSentenceEnd(index);
    } else {
      index = AddSymbol(index);
    }
  }
  return std::move(pieces_);
}

std::optional<size_t> Normalization::TryReading(size_t index) {
  constexpr size_t max_tokens = 6;
  constexpr std::string_view joins[] = {"-", "&", "/", "'", "’", ".", "!"};
  std::string written;
  std::string written_lower;
  std::string best;
  size_t best_end = index;
  for (auto end = index; end < tokens_.size() && end < index + max_tokens; ++end) {
    const auto& token = tokens_[end];
    if (
      token.type == TokenType::SYMBOL && std::find(std::begin(joins), std::end(joins), token.text) == std::end(joins)
    ) {
      break;
    }
    if (end > index && token.space_before) {
      written += ' ';
      written_lower += ' ';
    }
    written += token.text;
    written_lower += token.type == TokenType::WORD ? token.lower : token.text;
    // Searched only while a key of the table starts so.
    if (!words_.IsTablePrefix(written_lower)) {
      break;
    }
    if (end > index && token.type != TokenType::SYMBOL) {
      if (auto reading = words_.FromTable(written); !reading.empty()) {
        best = std::move(reading);
        best_end = end + 1;
      }
    }
  }
  if (best.empty()) {
    return std::nullopt;
  }
  Add(best, PieceKind::WORD, tokens_[index].space_before);
  return best_end;
}

size_t Normalization::AddWord(size_t index) {
  if (const auto end = TryReading(index)) {
    return *end;
  }
  if (const auto end = TryInitial(index)) {
    return *end;
  }
  if (const auto end = TryAbbreviation(index)) {
    return *end;
  }
  if (const auto end = TryNetAddress(index)) {
    return *end;
  }
  const auto& token = tokens_[index];
  size_t latin = 0;
  size_t letters = 0;
  size_t capitals = 0;
  for (size_t pos = 0; pos < token.text.size();) {
    const auto code_point = DecodeUtf8(token.text, pos);
    pos += code_point.size;
    // A combining mark goes with its letter: "Coco\u0301" is four Latin letters.
    if (code_point.value == U'+' || IsCombiningMark(code_point.value)) {
      continue;
    }
    ++letters;
    latin += IsLatinLetter(code_point.value) ? 1 : 0;
    capitals += ToLower(code_point.value) != code_point.value ? 1 : 0;
  }
  // "2x3", "2х3": "два на три".
  if ((token.lower == "x" || token.lower == "х") && index > 0 && IsNumber(index - 1) && IsNumber(index + 1)) {
    Add("на", PieceKind::WORD, token.space_before);
    return index + 1;
  }
  if (latin == 0) {
    constexpr size_t max_abbreviation = 5;
    const auto abbreviation = lower_cyrillic_ && letters >= 2 && letters <= max_abbreviation && capitals == letters
      && !lexicon_.IsServiceWord("word", token.lower) && !(is_word_ && is_word_(token.lower));
    Add(abbreviation ? words_.ReadAbbreviation(token.text) : token.lower, PieceKind::WORD, token.space_before);
    return index + 1;
  }
  if (latin < letters) {
    Add(ReadMixed(token.text), PieceKind::WORD, token.space_before);
    return index + 1;
  }
  if (lexicon_.IsServiceWord("garbage", token.lower)) {
    return index + 1;
  }
  // "c 5 до 7": a Latin "c" for the Russian one.
  if (
    token.text.size() == 1 && lower_cyrillic_ && std::string_view("acoyk").find(token.text) != std::string_view::npos
  ) {
    Add(RussianLetters(token.text, true), PieceKind::WORD, token.space_before);
    return index + 1;
  }
  // "Wi-Fi", "R&B", "AC/DC", "Don't": the table first, else word by word.
  constexpr std::string_view joins[] = {"-", "&", "/", "'", "’"};
  auto end = index + 1;
  std::string key = token.text;
  while (end + 1 < tokens_.size() && Glued(end) && Glued(end + 1) && IsLatinWord(end + 1)
         && std::find(std::begin(joins), std::end(joins), tokens_[end].text) != std::end(joins)) {
    key += tokens_[end].text + tokens_[end + 1].text;
    end += 2;
  }
  auto reading = words_.FromTable(key);
  if (reading.empty()) {
    std::string word = token.text;
    for (auto part = index + 1; part <= end; part += 2) {
      const auto join = part < end ? std::string_view(tokens_[part].text) : std::string_view();
      if (join == "'" || join == "’") {
        word += tokens_[part + 1].text;
        continue;
      }
      reading += words_.ReadLatin(word);
      reading += join == "-" ? "-" : (join == "&" ? " энд " : (join.empty() ? "" : " "));
      if (part < end) {
        word = tokens_[part + 1].text;
      }
    }
  }
  Add(reading, PieceKind::WORD, token.space_before);
  return end;
}

std::optional<size_t> Normalization::TryAbbreviation(size_t index) {
  constexpr size_t max_tokens = 8;
  std::string written;
  const Abbreviation* best = nullptr;
  size_t best_end = index;
  for (auto end = index; end < tokens_.size() && end < index + max_tokens; ++end) {
    const auto& token = tokens_[end];
    if (
      token.type == TokenType::NUMBER || (token.type == TokenType::SYMBOL && token.text != "." && token.text != "-")
    ) {
      break;
    }
    // Two words in a row keep their boundary: "в т. ч." is "в_т.ч.", "в т. 6" is not "вт.".
    if (end > index && token.type == TokenType::WORD && tokens_[end - 1].type == TokenType::WORD) {
      written += '_';
    }
    written += token.type == TokenType::WORD ? token.lower : token.text;
    if (!lexicon_.IsAbbreviationPrefix(written)) {
      break;
    }
    const auto* abbreviations = lexicon_.FindAbbreviation(written);
    // Words in a row are an abbreviation only with a dot or a hyphen: "и т. д.", not "в т" for "вт".
    if (abbreviations == nullptr || (end > index && written.find_first_of(".-") == std::string::npos)) {
      continue;
    }
    for (const auto& abbreviation : *abbreviations) {
      if (Holds(abbreviation.condition, index, end + 1)) {
        best = &abbreviation;
        best_end = end + 1;
        break;
      }
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  // "5 руб. Доставка завтра", "5 руб. «Доставка»", "и т. д. Потом": the dot ends the sentence too, unless a name follows the
  // abbreviation as usual ("г. Москва", "ул. Ленина").
  const auto ends_sentence = IsSymbol(best_end - 1, ".") && !best->before_name && CapitalAt(best_end);
  if (best->noun == nullptr) {
    Add(best->reading, PieceKind::WORD, tokens_[index].space_before);
    if (ends_sentence) {
      Add(".", PieceKind::END, false);
    }
    return best_end;
  }
  // A place: "в г+ороде", "на +улице", "к д+ому"; after "смотр+и" the accusative.
  auto governed = GovernedBefore(index);
  if (governed.empty() && !pieces_.empty() && pieces_.back().kind == PieceKind::WORD) {
    governed = lexicon_.GovernedCases(LookupKey(pieces_.back().text));
  }
  auto grammatical_case = kNom;
  if (best->noun->time || best->noun->animate) {
    // "в сред+у", "до понед+ельника", "у проф+ессора": the case the word names first.
    grammatical_case = governed.empty() ? kNom : governed.front();
  } else {
    for (const auto preferred : {Case::PREPOSITIONAL, Case::DATIVE, kGen, Case::INSTRUMENTAL, Case::ACCUSATIVE}) {
      if (std::find(governed.begin(), governed.end(), preferred) != governed.end()) {
        grammatical_case = preferred;
        break;
      }
    }
  }
  Add(best->noun->singular[static_cast<int>(grammatical_case)], PieceKind::WORD, tokens_[index].space_before);
  if (ends_sentence) {
    Add(".", PieceKind::END, false);
  }
  return best_end;
}

bool Normalization::Holds(Abbreviation::Condition condition, size_t begin, size_t end) const {
  switch (condition) {
    case Abbreviation::Condition::ALWAYS:
      return true;
    case Abbreviation::Condition::NUMBER:
      return IsNumber(end);
    case Abbreviation::Condition::NAME: {
      const auto quote = end < tokens_.size()
        && (tokens_[end].text == "«" || tokens_[end].text == "\"" || tokens_[end].text == "“");
      return quote || (IsWord(end) && StartsUpper(tokens_[end]))
        || (begin > 0 && IsWord(begin - 1) && StartsUpper(tokens_[begin - 1]));
    }
    case Abbreviation::Condition::ADDRESS: {
      // "д." earlier in the sentence.
      constexpr size_t max_distance = 16;
      for (auto index = begin; index > 0 && begin - index < max_distance; --index) {
        if (IsSymbol(index - 1, "?") || IsSymbol(index - 1, "!")) {
          return false;
        }
        if (
          IsWord(index - 1) && (tokens_[index - 1].lower == "д" || tokens_[index - 1].lower == "корп")
          && IsSymbol(index, ".")
        ) {
          return true;
        }
      }
      return false;
    }
  }
  return false;
}

std::optional<size_t> Normalization::TryNetAddress(size_t index) {
  if (!IsLatinWord(index)) {
    return std::nullopt;
  }
  constexpr std::string_view parts_list[] = {".", "@", "-", "_", "/", ":", "?", "=", "&", "%", "#", "~", "+"};
  constexpr size_t max_address_tokens = 64;
  auto end = index + 1;
  while (
    end < tokens_.size() && end < index + max_address_tokens && Glued(end)
    && (IsLatinWord(end) || IsNumber(end) || std::find(std::begin(parts_list), std::end(parts_list), tokens_[end].text) != std::end(parts_list))) {
    ++end;
  }
  while (end > index + 1 && tokens_[end - 1].type == TokenType::SYMBOL) {
    --end;
  }
  auto host = index;
  const auto scheme = (tokens_[index].lower == "http" || tokens_[index].lower == "https") && IsSymbol(index + 1, ":")
    && IsSymbol(index + 2, "/") && IsSymbol(index + 3, "/");
  if (scheme) {
    host = index + 4;
  }
  size_t at = 0;
  for (auto current = index; current < end; ++current) {
    if (IsSymbol(current, "@")) {
      at = current;
      break;
    }
  }
  if (at != 0) {
    host = at + 1;
  }
  if (host + 1 < end && tokens_[host].lower == "www" && IsSymbol(host + 1, ".")) {
    host += 2;
  }
  // The host: words and numbers between dots and hyphens.
  auto host_end = host;
  size_t dots = 0;
  while (host_end < end
         && (IsWord(host_end) || IsNumber(host_end) || IsSymbol(host_end, ".") || IsSymbol(host_end, "-"))) {
    dots += IsSymbol(host_end, ".") ? 1 : 0;
    ++host_end;
  }
  if (dots == 0 || host_end == host) {
    return std::nullopt;
  }
  Words words;
  const auto add_part = [&](size_t part) {
    if (IsNumber(part)) {
      words.Add(ReadGroup(tokens_[part].text));
    } else if (IsWord(part)) {
      words.Add(words_.ReadLatin(tokens_[part].text));
    } else if (IsSymbol(part, ".")) {
      words.Add("т+очка");
    }
  };
  if (at != 0) {
    for (auto part = scheme ? index + 4 : index; part < at; ++part) {
      add_part(part);
    }
    words.Add("соб+ака");
  }
  for (auto part = host; part < host_end; ++part) {
    add_part(part);
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::string Normalization::ReadMixed(std::string_view word) const {
  auto russian = RussianLetters(word);
  if (russian.size() > word.size()) {
    return russian;
  }
  Words parts;
  std::string part;
  bool latin_part = false;
  const auto flush = [&]() {
    parts.Add(latin_part ? words_.ReadLatin(part) : part);
    part.clear();
  };
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    // A combining mark goes with the letter before it.
    const auto latin = IsCombiningMark(code_point.value)
      ? latin_part
      : IsLatinLetter(code_point.value) || (code_point.value < 0x80 && code_point.value != U'+');
    if (!part.empty() && latin != latin_part) {
      flush();
    }
    latin_part = latin;
    if (latin) {
      part.append(word, pos, code_point.size);
    } else {
      AppendUtf8(part, ToLower(code_point.value));
    }
    pos += code_point.size;
  }
  if (!part.empty()) {
    flush();
  }
  return parts.Text();
}

size_t Normalization::AddSentenceEnd(size_t index) {
  const auto space_before = tokens_[index].space_before;
  bool question = false;
  bool exclamation = false;
  size_t dots = 0;
  std::vector<std::string_view> closing;
  for (; index < tokens_.size(); ++index) {
    const auto& text = tokens_[index].text;
    if (IsSentenceEnd(index)) {
      question = question || text == "?";
      exclamation = exclamation || text == "!";
      dots += text == "…" ? 2 : (text == "." ? 1 : 0);
    } else if (Glued(index) && (text == "»" || text == "”" || text == "\"" || text == ")")) {
      closing.push_back(OutputSymbol(text));
    } else {
      break;
    }
  }
  // "я жду... 2 минуты", "и... потом": an ellipsis before a lower-case word or a number is a pause within.
  const auto pause = !question && !exclamation && dots > 1 && closing.empty()
    && ((IsWord(index) && !StartsUpper(tokens_[index])) || IsNumber(index));
  if (pause) {
    Add(".", PieceKind::MARK, space_before);
    return index;
  }
  Add(question ? "?" : (exclamation ? "!" : "."), PieceKind::END, space_before);
  for (const auto mark : closing) {
    Add(mark, PieceKind::MARK, false);
  }
  counted_gender_.reset();
  return index;
}

std::optional<size_t> Normalization::TryInitial(size_t index) {
  const auto& token = tokens_[index];
  const auto single = [this](size_t at) {
    if (!IsWord(at) || !StartsUpper(tokens_[at])) {
      return false;
    }
    const auto code_point = Decode(tokens_[at].text, 0);
    return code_point && code_point->size == tokens_[at].text.size();
  };
  if (!single(index) || !IsSymbol(index + 1, ".") || !Glued(index + 1)) {
    return std::nullopt;
  }
  // A capitalized word after it, another initial too; an initial or a capitalized word before it.
  const auto name_after = IsWord(index + 2) && StartsUpper(tokens_[index + 2]);
  const auto initial_before = index >= 2 && IsSymbol(index - 1, ".") && single(index - 2);
  const auto name_before = index >= 1 && IsWord(index - 1) && StartsUpper(tokens_[index - 1]) && !single(index - 1);
  // "Буква А. Буква Б.": between two capitalized words, without an initial next to it, the dot ends a sentence.
  const auto initial_after = single(index + 2) && IsSymbol(index + 3, ".");
  if (
    !(name_after || initial_before || name_before) || (name_after && name_before && !initial_after && !initial_before)
  ) {
    return std::nullopt;
  }
  const auto latin = IsLatinWord(index);
  Add(latin ? SpellLatin(token.text) : SpellCyrillic(token.text), PieceKind::WORD, token.space_before);
  return index + 2;
}

size_t Normalization::SkipTo(size_t index, std::string_view close, bool russian_allowed) const {
  constexpr size_t max_markup_tokens = 64;
  for (auto end = index + 1; end < tokens_.size() && end < index + max_markup_tokens; ++end) {
    if (tokens_[end].text == close) {
      return end + 1;
    }
    if (
      !russian_allowed && tokens_[end].type == TokenType::WORD
      && static_cast<unsigned char>(tokens_[end].text[0]) >= 0x80
    ) {
      return index;
    }
  }
  return index;
}

size_t Normalization::AddSymbol(size_t index) {
  const auto& token = tokens_[index];
  const auto& text = token.text;
  const auto space = token.space_before;
  // "1,5–2,5 л": the bound of a range of decimals is spoken, the unit once after it.
  const auto range_dash = text == "-" || text == "–" || text == "—";
  if (
    range_dash && index >= 3 && IsNumber(index - 1) && (IsSymbol(index - 2, ",") || IsSymbol(index - 2, "."))
    && Glued(index - 1) && IsNumber(index - 3) && IsNumber(index + 1) && Glued(index + 1)
  ) {
    Add("-", PieceKind::CLAUSE, true);
    return index + 1;
  }
  // "<b>", "</speak>", "<!-- -->", "&amp;", "&#1055;", "[auto_wednesday]".
  if (text == "<" && Glued(index + 1) && (IsWord(index + 1) || IsSymbol(index + 1, "/") || IsSymbol(index + 1, "!"))) {
    if (const auto end = SkipTo(index, ">", true); end != index) {
      return end;
    }
  }
  if (text == "&" && Glued(index + 1) && (IsWord(index + 1) || IsSymbol(index + 1, "#"))) {
    if (const auto end = SkipTo(index, ";", false); end != index && end <= index + 4) {
      return end;
    }
  }
  // "[ссылка](https://…)": the address of a link is left out.
  if (
    text == "(" && IsWord(index + 1) && Glued(index + 1)
    && (tokens_[index + 1].lower == "http" || tokens_[index + 1].lower == "https" || tokens_[index + 1].lower == "www")
  ) {
    if (const auto end = SkipTo(index, ")", false); end != index) {
      return end;
    }
  }
  // "[auto_wednesday]", "{user_name}", "[img:1]": markup with "_", ":", "=", "/" or "#" inside is left out, so is a
  // placeholder "{0}"; "[12, 34]", "[yes]" are read, loss is worse than garbage.
  if (text == "[" || text == "{") {
    if (const auto end = SkipTo(index, text == "[" ? "]" : "}", false); end != index) {
      constexpr std::string_view markup_list[] = {"_", ":", "=", "/", "#"};
      const auto markup = std::any_of(
        tokens_.begin() + static_cast<std::ptrdiff_t>(index) + 1,
        tokens_.begin() + static_cast<std::ptrdiff_t>(end) - 1,
        [&](const Token& inside) {
          return std::any_of(std::begin(markup_list), std::end(markup_list), [&inside](std::string_view mark) {
            return inside.text.find(mark) != std::string::npos;
          });
        }
      );
      // "{0}", "${HOME}": a placeholder of a template or a variable.
      const auto placeholder = text == "{"
        && ((end == index + 3 && IsNumber(index + 1)) || (index > 0 && IsSymbol(index - 1, "$") && Glued(index)));
      if (markup || placeholder) {
        return end;
      }
    }
  }
  struct Spoken {
    std::string_view symbol;
    std::string_view words;
  };
  // Between two numbers.
  constexpr Spoken operators[] = {
    {"=", "равн+о"},
    {"×", "на"},
    {"*", "умн+ожить на"},
    {"÷", "раздел+ить на"},
    {"/", "раздел+ить на"},
    {"<", "м+еньше"},
    {">", "б+ольше"},
    {"≤", "м+еньше или равн+о"},
    {"≥", "б+ольше или равн+о"},
  };
  // "4.5/5": a rating, "из"; "6 / 2": a division.
  if (
    text == "/" && index >= 3 && IsNumber(index - 1) && IsNumber(index + 1) && Glued(index)
    && (IsSymbol(index - 2, ".") || IsSymbol(index - 2, ","))
  ) {
    Add("из", PieceKind::WORD, space);
    return index + 1;
  }
  if (index > 0 && IsNumber(index - 1) && (IsNumber(index + 1) || IsSign(index + 1))) {
    for (const auto& spoken : operators) {
      if (text == spoken.symbol) {
        Add(spoken.words, PieceKind::WORD, space);
        return index + 1;
      }
    }
  }
  // "Miyagi & Andy Panda", "свет & звук"; alone it is markup.
  if (text == "&") {
    if (index > 0 && IsWord(index - 1) && IsWord(index + 1)) {
      Add(IsLatinWord(index - 1) && IsLatinWord(index + 1) ? "энд" : "и", PieceKind::WORD, space);
    }
    return index + 1;
  }
  // ';' ends a sentence: a clause after it is read with the tune of a new sentence.
  if (text == ";") {
    Add(".", PieceKind::END, space);
    counted_gender_.reset();
    return index + 1;
  }
  constexpr Spoken words_list[] = {
    {"+", "плюс"},
    {"№", "н+омер"},
    {"§", "пар+аграф"},
    {"≈", "прим+ерно"},
  };
  for (const auto& spoken : words_list) {
    if (text == spoken.symbol) {
      Add(spoken.words, PieceKind::WORD, space);
      return index + 1;
    }
  }
  if (text == "~" && IsNumber(index + 1)) {
    Add("+около", PieceKind::WORD, space);
    return index + 1;
  }
  const auto symbol = OutputSymbol(text);
  constexpr std::string_view marks[] = {",", ";", ":", "-", "(", ")", "\""};
  if (std::find(std::begin(marks), std::end(marks), symbol) == std::end(marks)) {
    return index + 1;
  }
  // A dash between words, not a hyphen inside a word.
  const auto dash = symbol == "-" && (space || (index + 1 < tokens_.size() && tokens_[index + 1].space_before));
  const auto clause = symbol == "," || symbol == ";" || symbol == ":" || dash;
  Add(symbol, clause ? PieceKind::CLAUSE : PieceKind::MARK, space);
  return index + 1;
}

void Normalization::Add(std::string_view text, PieceKind kind, bool space_before) {
  if (text.empty()) {
    return;
  }
  // Empty quotes: "«»", "« null»".
  if (text == "\"" && !pieces_.empty() && pieces_.back().text == "\"") {
    pieces_.pop_back();
    return;
  }
  // A clause mark or a dot right before the end of a sentence gives way to it: "один:.", "гг..".
  while (
    kind == PieceKind::END && !pieces_.empty()
    && (pieces_.back().kind == PieceKind::CLAUSE || (pieces_.back().kind == PieceKind::MARK && pieces_.back().text == "."))) {
    space_before = space_before && pieces_.back().space_before;
    pieces_.pop_back();
  }
  // A run of clause marks is one mark, the strongest: "а,,,, б", "а, , б", "а —,— б", "а : , б"; as a run of ".?!" is
  // one end mark.
  if (kind == PieceKind::CLAUSE && !pieces_.empty() && pieces_.back().kind == PieceKind::CLAUSE) {
    constexpr std::string_view order = ",-:";
    auto& before = pieces_.back();
    if (order.find(text) != std::string_view::npos && order.find(text) > order.find(before.text)) {
      before.text = std::string(text);
    }
    return;
  }
  pieces_.push_back({std::string(text), kind, space_before});
}

} // namespace normalization

using normalization::Normalization;

TextNormalizer::TextNormalizer(
  IDataReader& reader,
  const std::string& directory,
  const SentenceParams& params,
  LatinWordReading read_word,
  WordCheck is_word,
  WordInfo word_info
)
  : lexicon_(reader, directory)
  , word_reader_(reader, directory, std::move(read_word))
  , params_(params)
  , is_word_(std::move(is_word))
  , word_info_(word_info ? std::move(word_info) : LoadMorphWordInfo(reader, directory, false)) {
  CheckSentenceParams(params_);
}

std::vector<NormalizedSentence> TextNormalizer::Normalize(std::string_view text) const {
  return SplitSentences(Normalization(lexicon_, word_reader_, is_word_, word_info_, text).Run(), params_);
}

} // namespace giga_edge::vits
