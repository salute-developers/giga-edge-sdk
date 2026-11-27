#include "giga_edge/modules/model/nodes/asr/predictions_to_text.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Predictions to text, ignore_duplicates, no eos", "[runtime][nodes]") {
  auto node = CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      blank_token: "<unk>"
      ignore_duplicates: true
    )",
    {{"token_dict",
      ToDataSpan(
        " ветер\n"
        " б\n"
        "лют\n"
        "у\n"
        "з\n"
        "<unk>\n"
        "<eos>"
      )}}
  );

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text") == " ветер ветер блютуз");
}

TEST_CASE("Predictions to text, with eos", "[runtime][nodes]") {
  auto node = CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      eos_token: "<eos>"
      ignore_duplicates: false
    )",
    {{"token_dict",
      ToDataSpan(
        " ветер\n"
        " б\n"
        "лют\n"
        "у\n"
        "з\n"
        "<unk>\n"
        "<eos>"
      )}}
  );

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      1.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      1.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text").empty());

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text").empty());

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      0.0,
      0.0,
      0.0,
      0.0,
      1.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      1.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text") == " ветер ветер блютуз");
}

TEST_CASE("Predictions to text, sentencepiece BPE", "[runtime][nodes]") {
  auto node = CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      blank_token: "<blank>"
      tokenizer_type: sentencepiece
    )",
    {{"token_dict",
      ToDataSpan(
        "<unk>\n"
        "<s>\n"
        "</s>\n"
        "<pad>\n"
        "▁\n"
        "ветер\n"
        "б\n"
        "лют\n"
        "у\n"
        "з\n"
        "<blank>"
      )}}
  );

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text") == "ветер ветер блютуз");

  NodePush(
    node,
    "predictions",
    std::vector<float>{
      0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text") == " ветер");

  node.node->Reset();
  NodePush(
    node,
    "predictions",
    std::vector<float>{
      0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
      0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
    }
  );
  REQUIRE(NodeGet<std::string>(node, "text") == "блютуз");
}

TEST_CASE("Predictions to text rejects unknown tokenizer type", "[runtime][nodes]") {
  REQUIRE_THROWS(CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      tokenizer_type: wordpiece
    )",
    {{"token_dict", ToDataSpan("token")}}
  ));
}

TEST_CASE("Predictions to text, sentence capitalization", "[runtime][nodes]") {
  auto node = CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      blank_token: "<blank>"
      tokenizer_type: sentencepiece
      capitalize_sentences: true
    )",
    {{"token_dict", ToDataSpan("▁привет\n.\n▁ёлка\n?\n▁the\n▁я\n,\n<blank>")}}
  );
  const auto one_hot = [](std::initializer_list<int> ids) {
    std::vector<float> result;
    for (const int id : ids) {
      std::vector<float> row(8, 0.0f);
      row[id] = 1.0f;
      result.insert(result.end(), row.begin(), row.end());
    }
    return result;
  };

  // A capital letter at the start of the text, after a period and a question mark; for yo (U+0451) and Latin too.
  NodePush(node, "predictions", one_hot({0, 1, 2, 3}));
  REQUIRE(NodeGet<std::string>(node, "text") == "Привет. Ёлка?");
  // The next chunk continues the sentence after "?".
  NodePush(node, "predictions", one_hot({4, 6, 5}));
  REQUIRE(NodeGet<std::string>(node, "text") == " The, я");

  node.node->Reset();
  NodePush(node, "predictions", one_hot({5, 0}));
  REQUIRE(NodeGet<std::string>(node, "text") == "Я привет");
}

TEST_CASE("Predictions to text, proper nouns", "[runtime][nodes]") {
  auto node = CreateNode(
    "predictions_to_text",
    R"(
      dict_path: "token_dict"
      blank_token: "<blank>"
      tokenizer_type: sentencepiece
      proper_nouns_path: "proper_nouns"
    )",
    {{"token_dict", ToDataSpan("▁в\n▁москв\nе\n.\n▁i\n▁мир\n<blank>")}, {"proper_nouns", ToDataSpan("москве\nиван\n")}},
    {"push"}
  );
  const auto step = [&node](std::initializer_list<int> ids, bool push) {
    std::vector<float> predictions;
    for (const int id : ids) {
      std::vector<float> row(7, 0.0f);
      row[id] = 1.0f;
      predictions.insert(predictions.end(), row.begin(), row.end());
    }
    NodePush(node, "push", push, true, 0);
    NodePush(node, "predictions", predictions);
    return NodeGet<std::string>(node, "text");
  };

  // A word of two chunks: the first part waits for the continuation, the capital comes once the word is whole.
  REQUIRE(step({0, 1}, false) == "в ");
  REQUIRE(step({2, 3}, false) == "Москве.");
  // After a period, lowercase without capitalize_sentences; the word is not in the proper nouns dictionary.
  REQUIRE(step({5, 4}, false) == " мир ");
  // The last word is released on push, the next utterance starts anew.
  REQUIRE(step({}, true) == "I");
  REQUIRE(step({0, 3}, true) == "в.");
}
