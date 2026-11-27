#include "giga_edge/modules/llm/kv_transcript.h"

#include <catch.hpp>

#include <algorithm>

using namespace giga_edge;

namespace {

// A tokenizer like the one in llama.cpp: the text is split at special tokens, the pieces between them are tokenized
// greedily and independently. That is why "vyklyuchi" gives "vy|klyu|chi", and "yklyuchi" alone gives "y|klyu|chi".
class FakeTokenizer {
public:
  static constexpr LlmToken kBos = 0;

  KvTranscript::Tokenizer Tokenize() {
    return [this](const std::string& text, bool prepend_bos) -> std::optional<std::vector<LlmToken>> {
      calls.push_back(text);
      return Ids(text, prepend_bos);
    };
  }

  KvTranscript::IsBoundary Boundary() const {
    return [](LlmToken token) {
      return token < 100;
    };
  }

  // The cache as the previous prompt would have left it.
  void Fill(KvTranscript& kv, const std::string& text, bool bos) {
    std::vector<uint32_t> ends;
    std::vector<LlmToken> ids;
    uint32_t end = 0;
    if (bos) {
      ends.push_back(0);
      ids.push_back(kBos);
    }
    for (const auto& piece : Pieces(text)) {
      end += static_cast<uint32_t>(piece.size());
      ends.push_back(end);
      ids.push_back(Id(piece));
    }
    kv.Append(text, ends, ids);
  }

  std::vector<LlmToken> Canonical(const std::string& text) const {
    return Ids(text, true);
  }

  std::vector<std::string> calls;

private:
  const std::vector<std::string> specials_ = {"<sep>", "<f_set>", "<arg>", "<end>"};
  const std::vector<std::string> words_ = {"вы", "в", "клю", "чи", " свет", "user\n", "assistant\n", "ok", "results\n"};

  // Like TokenizePromptPart of the engine: an empty text gives an empty list, even with a BOS.
  std::vector<LlmToken> Ids(const std::string& text, bool prepend_bos) const {
    std::vector<LlmToken> out;
    if (prepend_bos && !text.empty()) {
      out.push_back(kBos);
    }
    for (const auto& piece : Pieces(text)) {
      out.push_back(Id(piece));
    }
    return out;
  }

  std::vector<std::string> Pieces(const std::string& text) const {
    std::vector<std::string> out;
    std::string fragment;
    const auto flush = [&] {
      for (size_t i = 0; i < fragment.size();) {
        size_t best = 1;
        for (const auto& word : words_) {
          if (word.size() > best && fragment.compare(i, word.size(), word) == 0) {
            best = word.size();
          }
        }
        out.push_back(fragment.substr(i, best));
        i += best;
      }
      fragment.clear();
    };
    for (size_t i = 0; i < text.size();) {
      const auto special = std::find_if(specials_.begin(), specials_.end(), [&](const std::string& s) {
        return text.compare(i, s.size(), s) == 0;
      });
      if (special != specials_.end()) {
        flush();
        out.push_back(*special);
        i += special->size();
      } else {
        fragment += text[i++];
      }
    }
    flush();
    return out;
  }

  LlmToken Id(const std::string& piece) const {
    for (size_t i = 0; i < specials_.size(); ++i) {
      if (specials_[i] == piece) {
        return static_cast<LlmToken>(i + 1);
      }
    }
    for (size_t i = 0; i < words_.size(); ++i) {
      if (words_[i] == piece) {
        return static_cast<LlmToken>(i + 100);
      }
    }
    return static_cast<LlmToken>(static_cast<unsigned char>(piece.front())) + 1000;
  }
};

// What the model sees: the first keep_tokens tokens of the cache (also the start of the canonical tokenization) and
// the tail.
std::vector<LlmToken> Seen(const FakeTokenizer& tok, const std::string& prompt, const KvReusePlan& plan) {
  auto seen = tok.Canonical(prompt);
  seen.resize(plan.keep_tokens);
  seen.insert(seen.end(), plan.tail_tokens.begin(), plan.tail_tokens.end());
  return seen;
}

} // namespace

TEST_CASE("kv: an empty cache tokenizes the whole prompt with a BOS", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  const auto plan = kv.Plan("user\nвключи", tok.Tokenize(), tok.Boundary());
  REQUIRE(plan.has_value());
  REQUIRE(plan->keep_tokens == 0);
  REQUIRE(plan->tail == "user\nвключи");
  REQUIRE(plan->tail_tokens == tok.Canonical("user\nвключи"));
}

TEST_CASE("kv: the same prompt is reused whole, or all but one token when logits are needed", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\n<sep>ok<end>", true);
  const auto warm = kv.Plan("user\n<sep>ok<end>", tok.Tokenize(), tok.Boundary());
  REQUIRE(warm->keep_tokens == kv.Tokens());
  REQUIRE(warm->tail.empty());
  REQUIRE(warm->tail_tokens.empty());

  const auto generate = kv.Plan("user\n<sep>ok<end>", tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(generate->keep_tokens == kv.Tokens() - 1);
  REQUIRE(generate->tail == "<end>");
  REQUIRE(generate->tail_tokens.size() == 1);
}

TEST_CASE("kv: min_tail recomputes the last tokens, across special tokens and up to the whole prompt", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  const std::string prompt = "user\n<sep>включи свет<end>assistant\n";
  tok.Fill(kv, prompt, true);

  const auto plan = kv.Plan(prompt, tok.Tokenize(), tok.Boundary(), 4);
  REQUIRE(plan->tail_tokens.size() == 4);
  REQUIRE(plan->keep_tokens == kv.Tokens() - 4);
  REQUIRE(Seen(tok, prompt, *plan) == tok.Canonical(prompt));

  const auto whole = kv.Plan(prompt, tok.Tokenize(), tok.Boundary(), 100);
  REQUIRE(whole->keep_tokens == 0);
  REQUIRE(whole->tail == prompt);
  REQUIRE(Seen(tok, prompt, *whole) == tok.Canonical(prompt));
}

TEST_CASE("kv: a request that shares the start of a word with the previous one stays canonical", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\n<sep>включи свет<end>assistant\n", true);
  const std::string prompt = "user\n<sep>выключи свет<end>assistant\n";
  tok.calls.clear();
  const auto plan = kv.Plan(prompt, tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(plan.has_value());
  // The token "v" of "vklyuchi" is not reused: the model sees "vy|klyu|chi", as in the whole prompt.
  REQUIRE(plan->tail == "выключи свет<end>assistant\n");
  REQUIRE(Seen(tok, prompt, *plan) == tok.Canonical(prompt));
  // Only the tail after the last special token is tokenized.
  REQUIRE(tok.calls == std::vector<std::string>{"выключи свет<end>assistant\n"});
}

TEST_CASE("kv: a tool hop reuses the generated call and tokenizes only the new part", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\n<sep>включи свет<end>assistant\n", true);
  tok.Fill(kv, "<f_set><arg>ok<end>", false);
  const std::string prompt = "user\n<sep>включи свет<end>assistant\n<f_set><arg>ok<end>results\nok<end>assistant\n";
  tok.calls.clear();
  const auto plan = kv.Plan(prompt, tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(plan->keep_tokens == kv.Tokens());
  REQUIRE(plan->tail == "results\nok<end>assistant\n");
  REQUIRE(Seen(tok, prompt, *plan) == tok.Canonical(prompt));
  REQUIRE(tok.calls == std::vector<std::string>{"results\nok<end>assistant\n"});
}

TEST_CASE("kv: without boundary tokens the whole prompt is tokenized and still reused", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\nвключи свет", true);
  const std::string prompt = "user\nвыключи свет";
  const auto plan = kv.Plan(
    prompt,
    tok.Tokenize(),
    [](LlmToken) {
      return false;
    },
    1
  );
  REQUIRE(plan->keep_tokens == 2);
  REQUIRE(plan->tail == "выключи свет");
  REQUIRE(Seen(tok, prompt, *plan) == tok.Canonical(prompt));
}

TEST_CASE("kv: a boundary inside a span with coarse token ends is not used", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "", true);
  // The tokens of the piece did not add up to its text: all ends sit at the end of the piece.
  kv.Append("user\n<sep>ok", {12, 12, 12}, {tok.Canonical("user\n")[1], 1, tok.Canonical("ok")[1]});
  const std::string prompt = "user\n<sep>ok<end>";
  tok.calls.clear();
  const auto plan = kv.Plan(prompt, tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(Seen(tok, prompt, *plan) == tok.Canonical(prompt));
  REQUIRE(tok.calls == std::vector<std::string>{prompt});
}

TEST_CASE("kv: the cut falls back to an earlier boundary when the prompt ends on one", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\n<sep>ok<end>", true);
  const auto plan = kv.Plan("user\n<sep>ok<end>", tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(plan->tail_tokens.size() == 1);
  REQUIRE(plan->keep_tokens + 1 == kv.Tokens());
}

TEST_CASE("kv: truncate keeps bytes aligned with tokens", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  tok.Fill(kv, "user\n<sep>ok", true);
  kv.Truncate(2);
  REQUIRE(kv.Tokens() == 2);
  REQUIRE(kv.Text() == "user\n");
  kv.Truncate(0);
  REQUIRE(kv.Tokens() == 0);
  REQUIRE(kv.Text().empty());
}

TEST_CASE("kv: append rejects token ends that do not match the text or the tokens", "[llm][kv]") {
  KvTranscript kv;
  REQUIRE_THROWS(kv.Append("abc", {1}, {7}));
  REQUIRE_THROWS(kv.Append("abc", {3}, {7, 8}));
}

TEST_CASE("kv: an empty prompt plans nothing", "[llm][kv]") {
  FakeTokenizer tok;
  KvTranscript kv;
  const auto plan = kv.Plan("", tok.Tokenize(), tok.Boundary(), 1);
  REQUIRE(plan->keep_tokens == 0);
  REQUIRE(plan->tail.empty());
  REQUIRE(plan->tail_tokens.empty());
}
