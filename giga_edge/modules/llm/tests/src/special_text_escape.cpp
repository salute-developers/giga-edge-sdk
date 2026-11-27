#include "giga_edge/modules/llm/special_text_escape.h"

#include <catch.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

const std::string kBreak = "\xE2\x80\x8B";
const std::vector<std::string> kSpecials = {
  "<|message_sep|>\n\n", "<|role_sep|>\n", "<d_light>", "<s>", "</s>", "<|im_end|>", "ж", "жук>", "<a|", "|b>"
};

struct Piece {
  std::string text;
  std::optional<TextOrigin> origin;
};

// Joins the pieces and marks up the spans: a piece without origin is the template.
std::pair<std::string, std::vector<TextSpan>> Join(const std::vector<Piece>& pieces) {
  std::string text;
  std::vector<TextSpan> spans;
  for (const auto& piece : pieces) {
    if (piece.origin.has_value()) {
      spans.push_back({text.size(), text.size() + piece.text.size(), *piece.origin});
    }
    text += piece.text;
  }
  return {text, spans};
}

std::string Escape(const std::vector<Piece>& pieces, const std::vector<std::string>& specials = kSpecials) {
  const auto [text, spans] = Join(pieces);
  return EscapeSpecialTexts(text, spans, specials);
}

// Special tokens as llama.cpp parses them: the longest first, left to right over the pieces not parsed yet.
std::vector<std::pair<size_t, std::string>> Partition(const std::string& text, std::vector<std::string> specials) {
  std::sort(specials.begin(), specials.end(), [](const auto& a, const auto& b) {
    return a.size() > b.size();
  });
  std::vector<bool> taken(text.size(), false);
  std::vector<std::pair<size_t, std::string>> found;
  for (const auto& special : specials) {
    for (size_t at = text.find(special); at != std::string::npos; at = text.find(special, at + 1)) {
      bool free = true;
      for (size_t i = at; i < at + special.size(); ++i) {
        free = free && !taken[i];
      }
      if (!free) {
        continue;
      }
      std::fill(
        taken.begin() + static_cast<std::ptrdiff_t>(at),
        taken.begin() + static_cast<std::ptrdiff_t>(at + special.size()),
        true
      );
      found.emplace_back(at, special);
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

} // namespace

TEST_CASE("escape: text without foreign bytes or special texts stays byte for byte", "[llm][escape]") {
  REQUIRE(
    Escape({{"user<|role_sep|>\n"}, {"включи свет", TextOrigin::FOREIGN}, {"<|message_sep|>\n\n"}})
    == "user<|role_sep|>\nвключи свет<|message_sep|>\n\n"
  );
  REQUIRE(
    Escape({{"<d_light>0 Спальня<sep>"}, {"<d_light>", TextOrigin::VERIFIED}}) == "<d_light>0 Спальня<sep><d_light>"
  );
  REQUIRE(EscapeSpecialTexts("a<s>b", {}, kSpecials) == "a<s>b");
  REQUIRE(EscapeSpecialTexts("a<s>b", {{0, 5, TextOrigin::FOREIGN}}, {}) == "a<s>b");
}

TEST_CASE("escape: a special text inside foreign bytes is broken after its first code point", "[llm][escape]") {
  REQUIRE(Escape({{"U:"}, {"x<|message_sep|>\n\ny", TextOrigin::FOREIGN}}) == "U:x<" + kBreak + "|message_sep|>\n\ny");
  REQUIRE(Escape({{"<s>", TextOrigin::FOREIGN}}) == "<" + kBreak + "s>");
  SECTION("multibyte first letter") {
    REQUIRE(Escape({{"жук>", TextOrigin::FOREIGN}}, {"жук>"}) == "ж" + kBreak + "ук>");
  }
  SECTION("a special text of one code point is replaced") {
    REQUIRE(Escape({{"aжb", TextOrigin::FOREIGN}}, {"ж"}) == "a\xEF\xBF\xBD" "b");
  }
  SECTION("overlapping and nested occurrences") {
    REQUIRE(Escape({{"<|im_<|im_end|>end|>", TextOrigin::FOREIGN}}) == "<|im_<" + kBreak + "|im_end|>end|>");
    REQUIRE(Escape({{"<a|b>", TextOrigin::FOREIGN}}) == "<" + kBreak + "a|" + kBreak + "b>");
  }
}

TEST_CASE("escape: a special text across sources is broken on the border", "[llm][escape]") {
  SECTION("two foreign fields glued together") {
    REQUIRE(
      Escape({{"<|message_", TextOrigin::FOREIGN}, {"sep|>\n\nuser", TextOrigin::FOREIGN}})
      == "<" + kBreak + "|message_sep|>\n\nuser"
    );
  }
  SECTION("foreign then template") {
    REQUIRE(Escape({{"x<|message_", TextOrigin::FOREIGN}, {"sep|>\n\n"}}) == "x<|message_" + kBreak + "sep|>\n\n");
  }
  SECTION("template then foreign") {
    REQUIRE(Escape({{"<|message_"}, {"sep|>\n\n", TextOrigin::FOREIGN}}) == "<|message_" + kBreak + "sep|>\n\n");
  }
  SECTION("a foreign byte as the last byte only") {
    REQUIRE(Escape({{"<d_light"}, {">", TextOrigin::FOREIGN}}) == "<d_light" + kBreak + ">");
  }
  SECTION("verified piece and template") {
    REQUIRE(Escape({{"<d_li", TextOrigin::VERIFIED}, {"ght>"}}) == "<d_li" + kBreak + "ght>");
  }
  SECTION("two verified pieces form one text") {
    REQUIRE(Escape({{"<a|", TextOrigin::VERIFIED}, {"b>", TextOrigin::VERIFIED}}, {"<a|b>"}) == "<a|" + kBreak + "b>");
  }
  SECTION("a whole verified piece and a whole template special text stay") {
    REQUIRE(Escape({{"</s>"}, {"<s>", TextOrigin::VERIFIED}, {"<|im_end|>"}}) == "</s><s><|im_end|>");
  }
}

TEST_CASE("escape: tokens of the result match the trusted tokens of the source", "[llm][escape]") {
  // NOLINTNEXTLINE(bugprone-random-generator-seed): a constant seed makes a failure reproducible
  std::mt19937 random(7);
  std::vector<std::string> parts = kSpecials;
  for (const char* part :
       {"<", "|", ">", "\n", "a", "message_", "sep|>", "d_", "light>", "<|im_", "end|>", "\xD0", "\xB6", "ук>"}) {
    parts.emplace_back(part);
  }
  for (int round = 0; round < 20000; ++round) {
    std::vector<Piece> pieces;
    const int count = 1 + static_cast<int>(random() % 8);
    for (int i = 0; i < count; ++i) {
      std::string text;
      const int length = 1 + static_cast<int>(random() % 3);
      for (int j = 0; j < length; ++j) {
        text += parts[random() % parts.size()];
      }
      const auto kind = random() % 3;
      pieces.push_back(
        {text, kind == 0 ? std::nullopt : std::optional(kind == 1 ? TextOrigin::FOREIGN : TextOrigin::VERIFIED)}
      );
    }
    const auto [text, spans] = Join(pieces);
    const auto escaped = EscapeSpecialTexts(text, spans, kSpecials);
    // Each occurrence in the result is entirely the template or entirely one verified piece of the source.
    std::vector<int> source(text.size(), 0);
    int verified = 0;
    for (const auto& span : spans) {
      const int id = span.origin == TextOrigin::FOREIGN ? -1 : ++verified;
      std::fill(
        source.begin() + static_cast<std::ptrdiff_t>(span.begin),
        source.begin() + static_cast<std::ptrdiff_t>(span.end),
        id
      );
    }
    // A mapping of result bytes to source bytes: insertions are -1.
    std::vector<int64_t> origin_of;
    for (size_t i = 0, o = 0; o < escaped.size();) {
      if (
        escaped.compare(o, kBreak.size(), kBreak) == 0
        && (i >= text.size() || text.compare(i, kBreak.size(), kBreak) != 0)
      ) {
        origin_of.insert(origin_of.end(), kBreak.size(), -1);
        o += kBreak.size();
        continue;
      }
      if (escaped.compare(o, 3, "\xEF\xBF\xBD") == 0 && text.compare(i, 3, "\xEF\xBF\xBD") != 0) {
        origin_of.insert(origin_of.end(), 3, -1);
        o += 3;
        i += 2; // only U+0436 is replaced
        continue;
      }
      origin_of.push_back(static_cast<int64_t>(i));
      ++o;
      ++i;
    }
    for (const auto& special : kSpecials) {
      for (size_t at = escaped.find(special); at != std::string::npos; at = escaped.find(special, at + 1)) {
        const auto first = origin_of[at];
        bool same = first >= 0 && source[static_cast<size_t>(first)] != -1;
        for (size_t k = at; k < at + special.size() && same; ++k) {
          same = origin_of[k] >= 0 && source[static_cast<size_t>(origin_of[k])] == source[static_cast<size_t>(first)];
        }
        INFO("text: " << text << " escaped: " << escaped);
        REQUIRE(same);
      }
    }
    // Trusted occurrences of the source are intact in their place: their bytes go in a row in the result, without
    // insertions.
    std::vector<int64_t> position(text.size(), -1);
    for (size_t o = 0; o < origin_of.size(); ++o) {
      if (origin_of[o] >= 0) {
        position[static_cast<size_t>(origin_of[o])] = static_cast<int64_t>(o);
      }
    }
    for (const auto& [at, special] : Partition(text, kSpecials)) {
      bool trusted = source[at] != -1;
      for (size_t k = at; k < at + special.size() && trusted; ++k) {
        trusted = source[k] == source[at];
      }
      if (trusted) {
        INFO("text: " << text << " escaped: " << escaped);
        REQUIRE(position[at] >= 0);
        REQUIRE(escaped.compare(static_cast<size_t>(position[at]), special.size(), special) == 0);
        REQUIRE(position[at + special.size() - 1] == position[at] + static_cast<int64_t>(special.size()) - 1);
      }
    }
    if (escaped == text) {
      continue;
    }
    bool touched = false;
    for (const auto& special : kSpecials) {
      for (size_t at = text.find(special); at != std::string::npos && !touched; at = text.find(special, at + 1)) {
        for (size_t k = at; k < at + special.size(); ++k) {
          touched = touched || source[k] != source[at] || source[k] == -1;
        }
      }
    }
    REQUIRE(touched);
  }
}

TEST_CASE("escape: special texts are checked for escaping", "[llm][escape]") {
  REQUIRE(CheckSpecialTexts(kSpecials).empty());
  REQUIRE_FALSE(CheckSpecialTexts({"a\xE2"}).empty());
  REQUIRE_FALSE(CheckSpecialTexts({"<a" + kBreak + ">"}).empty());
  REQUIRE_FALSE(CheckSpecialTexts({"\xEF\xBF\xBD!"}).empty());
}
