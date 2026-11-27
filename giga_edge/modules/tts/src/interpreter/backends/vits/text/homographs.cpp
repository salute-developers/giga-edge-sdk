#include "giga_edge/modules/tts/interpreter/backends/vits/text/homographs.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace giga_edge::vits {

namespace {

std::string_view Trim(std::string_view text) {
  const auto begin = text.find_first_not_of(' ');
  if (begin == std::string_view::npos) {
    return {};
  }
  return text.substr(begin, text.find_last_not_of(' ') - begin + 1);
}

bool EndsWith(std::string_view text, std::string_view ending) {
  return text.size() >= ending.size() && text.substr(text.size() - ending.size()) == ending;
}

bool HasVowel(std::string_view word) {
  for (size_t i = 0; i < LetterCount(word); ++i) {
    if (IsVowel(LetterAt(word, i))) {
      return true;
    }
  }
  return false;
}

constexpr std::string_view kNoRule = "?";

} // namespace

HomographTable::HomographTable(std::string_view data, const std::string& name) {
  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> classes;
  std::map<std::string, std::string, std::less<>> templates;

  for (const auto& row : ReadTable(data)) {
    const auto fail = [&name, &row](const std::string& what) {
      ThrowTableError(name, row.line, what);
    };
    if (row.fields.size() < 2 || row.fields[0].empty()) {
      fail("expected at least two tab-separated fields");
    }
    const auto key = row.fields[0];
    if (key.front() == '@') {
      std::set<std::string, std::less<>> words;
      for (const auto word : Split(row.fields[1], ',')) {
        if (word.empty()) {
          fail("an empty word in the class '" + std::string(key) + "'");
        }
        words.insert(FoldYo(word));
      }
      if (!classes.emplace(std::string(key), std::move(words)).second) {
        fail("the class '" + std::string(key) + "' is repeated");
      }
      continue;
    }
    if (key.front() == '%') {
      if (!templates.emplace(std::string(key), std::string(row.fields[1])).second) {
        fail("the template '" + std::string(key) + "' is repeated");
      }
      continue;
    }

    const auto word = FoldYo(key);
    // A variant: the letters of the word up to е/ё with its stress, every '+' before a vowel.
    const auto check_variant = [&](std::string_view variant) {
      if (!IsStressedWord(variant) || FoldYo(WithoutMarks(variant)) != word) {
        fail("the variant '" + std::string(variant) + "' is not the word '" + std::string(key) + "'");
      }
      if (variant.find('+') == std::string_view::npos && HasVowel(variant)) {
        fail("the variant '" + std::string(variant) + "' has no stress");
      }
    };

    Row parsed;
    parsed.default_variant = std::string(row.fields[1]);
    check_variant(parsed.default_variant);

    std::string spec(row.fields.size() > 2 ? row.fields[2] : std::string_view());
    std::string variant_a;
    std::string variant_b;
    if (!spec.empty() && spec.front() == '%') {
      const auto open = spec.find('(');
      const auto comma = spec.find(',', open == std::string::npos ? 0 : open);
      if (open == std::string::npos || comma == std::string::npos || spec.back() != ')') {
        fail("expected %NAME(variantA,variantB)");
      }
      const auto found = templates.find(std::string_view(spec).substr(0, open));
      if (found == templates.end()) {
        fail("unknown template '" + spec.substr(0, open) + "'");
      }
      variant_a = spec.substr(open + 1, comma - open - 1);
      variant_b = spec.substr(comma + 1, spec.size() - comma - 2);
      spec = found->second;
    }

    for (const auto piece : Split(spec, ';')) {
      const auto text = Trim(piece);
      if (text.empty()) {
        continue;
      }
      const auto arrow = text.find("=>");
      if (arrow == std::string_view::npos) {
        fail("expected 'condition=>variant' in '" + std::string(text) + "'");
      }
      Rule rule;
      rule.variant = std::string(text.substr(arrow + 2));
      if (!variant_a.empty() && rule.variant == "A") {
        rule.variant = variant_a;
      } else if (!variant_b.empty() && rule.variant == "B") {
        rule.variant = variant_b;
      }
      if (rule.variant == kNoRule) {
        rule.variant.clear();
      } else {
        check_variant(rule.variant);
      }
      for (const auto condition_text : Split(text.substr(0, arrow), '&')) {
        const auto colon = condition_text.find(':');
        if (colon == std::string_view::npos) {
          fail("expected 'kind:values' in '" + std::string(condition_text) + "'");
        }
        const auto kind = condition_text.substr(0, colon);
        static constexpr std::pair<std::string_view, std::pair<Neighbour, Match>> kinds[] = {
          {"p", {Neighbour::PREVIOUS, Match::WHOLE}},
          {"n", {Neighbour::NEXT, Match::WHOLE}},
          {"pp", {Neighbour::BEFORE_PREVIOUS, Match::WHOLE}},
          {"nn", {Neighbour::AFTER_NEXT, Match::WHOLE}},
          {"ps", {Neighbour::PREVIOUS, Match::ENDING}},
          {"ns", {Neighbour::NEXT, Match::ENDING}},
          {"pst", {Neighbour::PREVIOUS, Match::BEGINNING}},
          {"nst", {Neighbour::NEXT, Match::BEGINNING}},
          {"rs", {Neighbour::ANY_FOLLOWING, Match::ENDING}},
        };
        const auto* known = std::find_if(std::begin(kinds), std::end(kinds), [kind](const auto& entry) {
          return entry.first == kind;
        });
        if (known == std::end(kinds)) {
          fail("unknown condition '" + std::string(kind) + "'");
        }
        Condition condition;
        condition.neighbour = known->second.first;
        condition.match = known->second.second;
        for (const auto value : Split(condition_text.substr(colon + 1), ',')) {
          if (value.empty()) {
            fail("an empty value in '" + std::string(condition_text) + "'");
          }
          if (value.front() == '@') {
            const auto found = classes.find(value);
            if (found == classes.end()) {
              fail("unknown class '" + std::string(value) + "'");
            }
            condition.values.insert(found->second.begin(), found->second.end());
          } else {
            condition.values.insert(FoldYo(value));
          }
        }
        rule.conditions.push_back(std::move(condition));
      }
      parsed.rules.push_back(std::move(rule));
    }
    parsed.variants.push_back(parsed.default_variant);
    for (const auto& rule : parsed.rules) {
      if (
        !rule.variant.empty()
        && std::find(parsed.variants.begin(), parsed.variants.end(), rule.variant) == parsed.variants.end()
      ) {
        parsed.variants.push_back(rule.variant);
      }
    }
    if (!rows_.emplace(word, std::move(parsed)).second) {
      fail("the word '" + std::string(key) + "' is already in the table");
    }
  }
}

const std::vector<std::string>* HomographTable::Variants(std::string_view word) const {
  const auto found = rows_.find(word);
  return found == rows_.end() ? nullptr : &found->second.variants;
}

std::optional<HomographTable::Choice> HomographTable::Apply(
  std::string_view word,
  std::string_view previous,
  std::string_view next,
  std::string_view before_previous,
  std::string_view after_next,
  const std::string* following,
  size_t following_count
) const {
  const auto found = rows_.find(word);
  if (found == rows_.end()) {
    return std::nullopt;
  }
  const std::string_view neighbours[] = {previous, next, before_previous, after_next}; // in the order of Neighbour
  const auto holds = [&](const Condition& condition) {
    if (condition.neighbour == Neighbour::ANY_FOLLOWING) {
      return std::any_of(following, following + following_count, [&](const std::string& other) {
        return std::any_of(condition.values.begin(), condition.values.end(), [&](const std::string& value) {
          return EndsWith(other, value) && other.size() > value.size();
        });
      });
    }
    const auto neighbour = neighbours[static_cast<size_t>(condition.neighbour)];
    if (condition.match == Match::WHOLE) {
      return condition.values.find(neighbour) != condition.values.end();
    }
    if (neighbour == kNoWordBefore || neighbour == kNoWordAfter) {
      return false;
    }
    return std::any_of(condition.values.begin(), condition.values.end(), [&](const std::string& value) {
      return condition.match == Match::ENDING
        ? EndsWith(neighbour, value) && neighbour.size() > value.size()
        : neighbour.substr(0, value.size()) == value;
    });
  };
  for (const auto& rule : found->second.rules) {
    if (std::all_of(rule.conditions.begin(), rule.conditions.end(), holds)) {
      if (rule.variant.empty()) {
        break; // no rule decides here
      }
      return Choice{rule.variant, true};
    }
  }
  return Choice{found->second.default_variant, false};
}

} // namespace giga_edge::vits
