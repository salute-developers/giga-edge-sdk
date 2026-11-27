#include "giga_edge/modules/common/nodes/strip_leading_words/node.h"

#include "giga_edge/lib/common/str_utils.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace giga_edge {

namespace {

using Word = StripLeadingWordsNode::Word;

constexpr size_t kMinGluedRest = 2;

char16_t NormalizeChar(char16_t ch) {
  const auto lower = ToLowerLatinCyrillic(ch);
  return lower == u'ё' ? u'е' : lower;
}

bool IsDigit(char16_t ch) {
  return ch >= u'0' && ch <= u'9';
}

bool IsSeparator(char16_t ch) {
  return !IsAlpha(ch) && !IsDigit(ch);
}

Word ToWord(std::string_view text) {
  Word word;
  for (const auto& token : NormalizeTextToTokens(std::string(text), NormalizeChar).tokens) {
    word.push_back(ToUtf16(token));
  }
  return word;
}

size_t EditDistance(const std::u16string& lhs, const std::u16string& rhs) {
  std::vector<size_t> row(rhs.size() + 1);
  std::iota(row.begin(), row.end(), 0);
  for (size_t i = 1; i <= lhs.size(); ++i) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= rhs.size(); ++j) {
      const size_t above = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (lhs[i - 1] == rhs[j - 1] ? 0 : 1)});
      diagonal = above;
    }
  }
  return row.back();
}

// A common Latin spelling by sound: a bilingual ASR writes the same word now in Cyrillic, now in Latin.
std::u16string PhoneticKey(const std::u16string& token) {
  static const std::unordered_map<char16_t, std::u16string_view> sounds = {
    {u'а', u"a"}, {u'б', u"b"},  {u'в', u"v"},  {u'г', u"g"},  {u'д', u"d"},   {u'е', u"e"}, {u'ж', u"zh"},
    {u'з', u"z"}, {u'и', u"i"},  {u'й', u"y"},  {u'к', u"k"},  {u'л', u"l"},   {u'м', u"m"}, {u'н', u"n"},
    {u'о', u"o"}, {u'п', u"p"},  {u'р', u"r"},  {u'с', u"s"},  {u'т', u"t"},   {u'у', u"u"}, {u'ф', u"f"},
    {u'х', u"h"}, {u'ц', u"ts"}, {u'ч', u"ch"}, {u'ш', u"sh"}, {u'щ', u"sch"}, {u'ъ', u""},  {u'ы', u"y"},
    {u'ь', u""},  {u'э', u"e"},  {u'ю', u"yu"}, {u'я', u"ya"}, {u'x', u"ks"},  {u'w', u"v"},
  };
  std::u16string key;
  for (size_t i = 0; i < token.size(); ++i) {
    if (token[i] == u'д' && i + 1 < token.size() && token[i + 1] == u'ж') {
      key.push_back(u'j');
      ++i;
      continue;
    }
    const auto sound = sounds.find(token[i]);
    if (sound == sounds.end()) {
      key.push_back(token[i]);
    } else {
      key += sound->second;
    }
  }
  return key;
}

bool WithinTypos(const std::u16string& token, const std::u16string& expected, size_t typos) {
  return token == expected || (typos > 0 && EditDistance(token, expected) <= typos);
}

bool TokensMatch(
  const std::u16string& token, const std::u16string& expected, const StripLeadingWordsNodeParams& params
) {
  const size_t typos = params.typos_per_letters > 0 ? expected.size() / params.typos_per_letters : 0;
  return WithinTypos(token, expected, typos) || WithinTypos(PhoneticKey(token), PhoneticKey(expected), typos);
}

// ASR sometimes glues a word to the next one: strip the beginning that matches by sound.
std::optional<size_t> GluedLength(const std::u16string& token, const std::u16string& expected) {
  const auto key = PhoneticKey(expected);
  for (size_t length = 1; length + kMinGluedRest <= token.size(); ++length) {
    if (PhoneticKey(token.substr(0, length)) == key) {
      return length;
    }
  }
  return std::nullopt;
}

std::optional<size_t> MatchWord(
  const std::u16string& text, size_t pos, const Word& word, const StripLeadingWordsNodeParams& params, bool split_glued
) {
  for (size_t i = 0; i < word.size(); ++i) {
    while (pos < text.size() && IsSeparator(text[pos])) {
      ++pos;
    }
    const size_t start = pos;
    std::u16string token;
    while (pos < text.size() && IsAlpha(text[pos])) {
      token.push_back(NormalizeChar(text[pos++]));
    }
    if (token.empty()) {
      return std::nullopt;
    }
    if (TokensMatch(token, word[i], params)) {
      continue;
    }
    if (split_glued && i + 1 == word.size()) {
      if (const auto length = GluedLength(token, word[i])) {
        return start + *length;
      }
    }
    return std::nullopt;
  }
  return pos;
}

size_t SkipToken(const std::u16string& text, size_t pos) {
  while (pos < text.size() && IsSeparator(text[pos])) {
    ++pos;
  }
  while (pos < text.size() && !IsSeparator(text[pos])) {
    ++pos;
  }
  return pos;
}

bool StartsCapitalized(const std::u16string& text) {
  const auto letter = std::find_if(text.begin(), text.end(), IsAlpha);
  return letter != text.end() && ToLowerLatinCyrillic(*letter) != *letter;
}

} // namespace

StripLeadingWordsNode::StripLeadingWordsNode(
  const NodeBaseConfig& base_config, const StripLeadingWordsNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {
  for (const auto& text : params_.words) {
    if (auto word = ToWord(text); !word.empty()) {
      configured_words_.push_back(std::move(word));
    }
  }
}

void StripLeadingWordsNode::ProcessOnce() {
  const auto& text = GetInput();
  const auto& input_words = InputWords();
  const auto text16 = ToUtf16(text);

  const auto match_longest = [&](size_t pos, bool split_glued) {
    std::optional<size_t> longest;
    const auto consider = [&](const std::vector<Word>& words) {
      for (const auto& word : words) {
        const auto end = MatchWord(text16, pos, word, params_, split_glued);
        if (end && (!longest || *end > *longest)) {
          longest = end;
        }
      }
    };
    consider(configured_words_);
    consider(input_words);
    return longest;
  };

  auto end = match_longest(0, params_.split_glued);
  for (size_t pos = 0, skipped = 0; !end && skipped < static_cast<size_t>(params_.max_skipped_tokens); ++skipped) {
    pos = SkipToken(text16, pos);
    end = match_longest(pos, params_.split_glued);
  }
  if (!end) {
    const bool had_words = !configured_words_.empty() || !input_words.empty();
    PushOutput(params_.drop_unmatched && had_words ? std::string() : text);
    return;
  }
  size_t cut = *end;
  while (params_.repeat) {
    const auto next = match_longest(cut, false);
    if (!next) {
      break;
    }
    cut = *next;
  }
  while (cut < text16.size() && IsSeparator(text16[cut])) {
    ++cut;
  }
  auto rest = text16.substr(cut);
  if (params_.capitalize_rest && !rest.empty() && StartsCapitalized(text16)) {
    rest.front() = ToUpperLatinCyrillic(rest.front());
  }
  PushOutput(ToUtf8(rest));
}

const std::vector<Word>& StripLeadingWordsNode::InputWords() {
  const auto words = GetWords();
  const std::string_view source = words.has_value() ? std::string_view(words->get()) : std::string_view{};
  // The words arrive on the trigger tick and the text later: an empty input does not erase them.
  if (source.empty() && params_.hold_words) {
    return input_words_;
  }
  if (source != input_words_source_) {
    input_words_source_ = std::string(source);
    input_words_.clear();
    for (const auto piece : SplitString(input_words_source_, params_.words_delimiter)) {
      if (auto word = ToWord(piece); !word.empty()) {
        input_words_.push_back(std::move(word));
      }
    }
  }
  return input_words_;
}

} // namespace giga_edge
