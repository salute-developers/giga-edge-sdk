#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/question_rules.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace giga_edge::vits {
namespace {

// 'ё' -> 'е': both take two bytes, so the offsets stay.
std::string FoldYo(std::string text) {
  for (auto pos = text.find("ё"); pos != std::string::npos; pos = text.find("ё", pos)) {
    text.replace(pos, 2, "е");
  }
  return text;
}

bool Contains(const std::vector<std::string>& list, std::string_view word) {
  return std::find(list.begin(), list.end(), word) != list.end();
}

} // namespace

QuestionRules::QuestionRules(std::string_view json, const std::string& name) {
  const auto fail = [&name](const std::string& message) {
    throw std::runtime_error("vits question rules '" + name + "': " + message);
  };
  const auto root = ParseJson(json, name);
  if (!root.isObject()) {
    fail("must be a JSON object");
  }
  const std::pair<const char*, std::vector<std::string>*> lists[] = {
    {"words", &words_},
    {"sentence_starts", &sentence_starts_},
    {"before_particles", &before_particles_},
    {"before_question_mark", &before_question_mark_},
    {"contrast_conjunctions", &contrast_conjunctions_},
  };
  for (const auto& key : root.getMemberNames()) {
    const auto list = std::find_if(std::begin(lists), std::end(lists), [&key](const auto& l) {
      return key == l.first;
    });
    if (list == std::end(lists)) {
      fail("unknown list '" + key + "'");
    }
    if (!root[key].isArray()) {
      fail("'" + key + "' must be an array of strings");
    }
    for (const auto& item : root[key]) {
      if (!item.isString() || item.asString().empty()) {
        fail("'" + key + "' must hold non-empty strings");
      }
      auto word = FoldYo(item.asString());
      if (!Contains(*list->second, word)) {
        list->second->push_back(std::move(word));
      }
    }
  }
}

std::vector<TextRange> QuestionRules::Find(const PreparedSentence& sentence) const {
  CheckWords(sentence);
  const auto text = FoldYo(sentence.plain);
  if (text.find('?') == std::string::npos) {
    return {};
  }
  // Words joined by a bare '-' are one word, as in the language data: "кто-то" is not "кто".
  std::vector<TextRange> words;
  for (const auto& word : sentence.words) {
    const auto& range = word.plain;
    if (!words.empty() && words.back().begin + words.back().size + 1 == range.begin && text[range.begin - 1] == '-') {
      words.back().size = range.begin + range.size - words.back().begin;
    } else {
      words.push_back(range);
    }
  }
  const auto end_of = [&words](size_t i) {
    return words[i].begin + words[i].size;
  };
  const auto word_at = [&](size_t i) {
    return std::string_view(text).substr(words[i].begin, words[i].size);
  };
  const auto before_question_mark = [&](size_t i) {
    return end_of(i) < text.size() && text[end_of(i)] == '?';
  };
  const auto space_after = [&](size_t i) {
    return i + 1 < words.size() && end_of(i) + 1 == words[i + 1].begin && text[end_of(i)] == ' ';
  };

  std::vector<TextRange> spans;
  bool keep_default = true;
  const auto add = [&](size_t begin, size_t end, bool in_any_clause) {
    spans.push_back({begin, end - begin});
    keep_default = keep_default && !in_any_clause && text.find(',', end) != std::string::npos;
  };
  for (const auto& start : sentence_starts_) {
    if (text.compare(0, start.size(), start) == 0) {
      add(0, start.size(), false);
    }
  }
  for (size_t i = 0; i < words.size(); ++i) {
    for (const auto& phrase : words_) {
      const auto end = words[i].begin + phrase.size();
      if (text.compare(words[i].begin, phrase.size(), phrase) != 0) {
        continue;
      }
      for (auto last = i; last < words.size() && end_of(last) <= end; ++last) {
        if (end_of(last) == end) {
          add(words[i].begin, end, false);
        }
      }
    }
    if (i > 0 && Contains(before_particles_, word_at(i))) {
      add(words[i - 1].begin, end_of(i - 1), true);
    }
    if (before_question_mark(i) && Contains(before_question_mark_, word_at(i))) {
      add(words[i].begin, end_of(i), true);
    }
    if (Contains(contrast_conjunctions_, word_at(i)) && space_after(i)) {
      const auto begin = words[i].begin;
      if (begin > 0 && (text[begin - 1] == ' ' || text[begin - 1] == ',') && before_question_mark(i + 1)) {
        add(begin, end_of(i + 1), true);
      }
      if (
        words[i + 1].size > 0 && DecodeUtf8(text, words[i + 1].begin).size == words[i + 1].size && space_after(i + 1)
        && before_question_mark(i + 2)
      ) {
        add(begin, end_of(i + 2), true);
      }
    }
  }
  const auto space = text.rfind(' ');
  if (keep_default && space != std::string::npos) {
    spans.push_back({space, text.size() - space});
  }
  return spans;
}

} // namespace giga_edge::vits
