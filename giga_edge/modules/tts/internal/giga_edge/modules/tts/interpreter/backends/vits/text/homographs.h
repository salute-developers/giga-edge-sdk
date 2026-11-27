#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// Homographs: the default reading of a word and rules that choose another one by the neighbouring words.
// The table (homographs.tsv):
//   @NAME<TAB>words by commas                      a class of words
//   %NAME<TAB>rules with the variants A and B      a template of rules
//   word<TAB>default<TAB>rules | %NAME(variantA,variantB)
// Rules are "condition[&condition...]=>variant" separated by ';'; the first one that holds wins. The variant "?"
// stops the rules: the word is read as if none held (by the model or the default). A condition is
// "kind:values by commas", a value is a word or a @NAME class:
//   p, n, pp, nn    the previous word, the next one, the one before the previous, the one after the next is in the
//                   list ('^': no word on the left, '$': none on the right)
//   ps, ns          the previous / next word ends with a value and is longer than it
//   pst, nst        the previous / next word starts with a value
//   rs              some word further right in the same piece ends with a value and is longer than it
// A variant is the word with '+' before the stressed vowels (none for a word without vowels); it may differ from
// the word in е/ё only. Words are compared without the difference between 'е' and 'ё'. A word, a class and a
// template stand in the table once; a value of a condition is not empty.
class HomographTable {
public:
  // Throws std::runtime_error "vits: <name>:<line>: ..." on a bad line.
  HomographTable(std::string_view data, const std::string& name);

  struct Choice {
    std::string_view variant; // lives as long as the table
    bool by_rule = false;
  };

  // The neighbours are words with 'е' for 'ё'; kNoWordBefore / kNoWordAfter when a mark or the edge is there.
  // `following`: the `following_count` words after the word up to the mark or the edge, `next` the first of them.
  std::optional<Choice> Apply(
    std::string_view word,
    std::string_view previous,
    std::string_view next,
    std::string_view before_previous,
    std::string_view after_next,
    const std::string* following = nullptr,
    size_t following_count = 0
  ) const;

  // The readings of a word of the table: the default one, then those of its rules; nullptr for another word.
  const std::vector<std::string>* Variants(std::string_view word) const;

  static constexpr std::string_view kNoWordBefore = "^";
  static constexpr std::string_view kNoWordAfter = "$";

private:
  enum class Neighbour { PREVIOUS, NEXT, BEFORE_PREVIOUS, AFTER_NEXT, ANY_FOLLOWING };
  enum class Match { WHOLE, ENDING, BEGINNING };
  struct Condition {
    Neighbour neighbour = Neighbour::PREVIOUS;
    Match match = Match::WHOLE;
    std::set<std::string, std::less<>> values;
  };
  struct Rule {
    std::vector<Condition> conditions;
    std::string variant;
  };
  struct Row {
    std::string default_variant;
    std::vector<Rule> rules;
    std::vector<std::string> variants;
  };

  std::map<std::string, Row, std::less<>> rows_;
};

} // namespace giga_edge::vits
