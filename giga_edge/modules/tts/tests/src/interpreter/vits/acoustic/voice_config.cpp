#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"

#include "helpers.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;
using Catch::Matchers::Contains;

namespace {

// The test voice config with one replacement in its text.
VoiceConfig Edited(const std::string& from, const std::string& to) {
  auto text = ToString(CreateDataReader("data/")->ReadTransientData("vits/synthetic/voice.json"));
  const auto pos = text.find(from);
  REQUIRE(pos != std::string::npos);
  REQUIRE(text.find(from, pos + 1) == std::string::npos);
  return ParseVoiceConfig(text.replace(pos, from.size(), to), "edited");
}

} // namespace

TEST_CASE("vits voice config: model_config.json of a voice", "[tts][vits]") {
  const auto voice = test::TestVoice();
  CHECK(voice.alphabet.size() == 58);
  CHECK(voice.alphabet.substr(0, 3) == U"_ ~");
  CHECK(voice.alphabet[20] == U'ё');
  CHECK(voice.alphabet.back() == U'^');
  CHECK(voice.feature_names == std::vector<std::string>{"loudness", "mean_f0", "left_slope", "right_slope", "speed"});
  CHECK(voice.embedding_dims == std::vector<int64_t>{5, 5, 5, 5, 5});
  REQUIRE(voice.forced_labels.size() == 1);
  CHECK(voice.forced_labels[0].feature == 0);
  CHECK(voice.forced_labels[0].label == 2);
  REQUIRE(voice.question_labels.size() == 4);
  CHECK(voice.use_question_rules);
  CHECK(voice.blank_intersperse);
  CHECK(voice.length_scale == 1.0F);
  CHECK(voice.noise_scale == 0.2F);
  CHECK(voice.noise_scale_w == 0.4F);
  CHECK(voice.gain_db == 17.0F);
  CHECK(voice.sample_rate == 24000);
  CHECK(voice.streaming.num_frames == 32);
  CHECK(voice.streaming.flow_overlap == 23);
  CHECK(voice.streaming.decoder_overlap == 13);
  CHECK_FALSE(Edited("\"use_questions_rules\": true,", "").use_question_rules);
  CHECK(Edited("\"force_feature_label_dict\"", "\"unused\"").forced_labels.empty());
}

TEST_CASE("vits voice config: a broken config throws", "[tts][vits]") {
  CHECK_THROWS_WITH(ParseVoiceConfig("{", "bad"), Contains("'bad' is not valid JSON"));
  CHECK_THROWS_WITH(ParseVoiceConfig("[]", "bad"), Contains("must be a JSON object"));
  CHECK_THROWS_WITH(ParseVoiceConfig("{} {}", "bad"), Contains("not valid JSON"));
  CHECK_THROWS_WITH(Edited("\"gain_db\": 17,", "\"gain_db\": 17, \"gain_db\": 1,"), Contains("not valid JSON"));
  CHECK_THROWS_WITH(Edited("\"model_sample_rate\"", "\"rate\""), Contains("missing 'model_sample_rate'"));
  CHECK_THROWS_WITH(Edited("\"_ ~", "\"_ _~"), Contains("repeats '_'"));
  CHECK_THROWS_WITH(Edited("\"_ ~", "\"\xD0 ~"), Contains("not valid UTF-8"));
  CHECK_THROWS_WITH(
    Edited("\"_ ~*.,;:?!-()\\\"абвгдеёжзийклмнопрстуфхцчшщъыьэюяАЯОЁУЮЫИЭЕ^\"", "\"\""), Contains("non-empty string")
  );
  CHECK_THROWS_WITH(Edited("\"speed\"\n", "\"speed\", \"x\"\n"), Contains("arrays of the same size"));
  CHECK_THROWS_WITH(Edited("\"speed\"\n", "\"loudness\"\n"), Contains("repeats 'loudness'"));
  CHECK_THROWS_WITH(Edited("\"loudness\": 2", "\"pitch\": 2"), Contains("unknown feature 'pitch'"));
  CHECK_THROWS_WITH(Edited("\"loudness\": 2", "\"loudness\": 5"), Contains("must be an integer in [0, 5)"));
  CHECK_THROWS_WITH(Edited("\"loudness\": 3", "\"loudness\": -1"), Contains("must be an integer in [0, 5)"));
  CHECK_THROWS_WITH(Edited("\"blank_intersperse\": true", "\"blank_intersperse\": 1"), Contains("true or false"));
  CHECK_THROWS_WITH(Edited("\"length_scale\": 1.0", "\"length_scale\": 0"), Contains("'length_scale' must be > 0"));
  CHECK_THROWS_WITH(Edited("\"noise_scale\": 0.2", "\"noise_scale\": -1"), Contains("'noise_scale' must be >= 0"));
  CHECK_THROWS_WITH(
    Edited("\"noise_scale_w\": 0.4", "\"noise_scale_w\": \"0.4\""), Contains("must be a finite number")
  );
  CHECK_THROWS_WITH(Edited("\"num_frames\": 32", "\"num_frames\": 13"), Contains("less than 'num_frames'"));
  CHECK_THROWS_WITH(Edited("\"num_frames\": 32", "\"num_frames\": 0"), Contains("integer in [1, 1024]"));
  CHECK_THROWS_WITH(Edited("\"flow_overlap\": 23", "\"flow_overlap\": 2.5"), Contains("integer in [0, 1024]"));
  CHECK_THROWS(ReadVoiceConfig(CreateDataReader("data/"), "vits/missing.json"));
}

TEST_CASE("vits voice config: numbers are finite and integers bounded", "[tts][vits]") {
  // Finite as a double, infinite as a float; past a double the JSON reader may refuse the number itself.
  CHECK_THROWS_WITH(Edited("\"gain_db\": 17", "\"gain_db\": 1e39"), Contains("'gain_db' must be a finite number"));
  CHECK_THROWS_WITH(Edited("\"length_scale\": 1.0", "\"length_scale\": 1e39"), Contains("must be a finite number"));
  CHECK_THROWS(Edited("\"gain_db\": 17", "\"gain_db\": 1e400"));
  CHECK_THROWS_WITH(Edited("\"num_frames\": 32", "\"num_frames\": 1025"), Contains("integer in [1, 1024]"));
  CHECK_THROWS_WITH(
    Edited("\"num_frames\": 32", "\"num_frames\": 9223372036854775807"), Contains("integer in [1, 1024]")
  );
  CHECK_THROWS_WITH(Edited("\"flow_overlap\": 23", "\"flow_overlap\": 1025"), Contains("integer in [0, 1024]"));
  CHECK_THROWS_WITH(
    Edited("\"model_sample_rate\": 24000", "\"model_sample_rate\": 4294967296"), Contains("integer in [8000, 192000]")
  );
  CHECK_THROWS_WITH(
    Edited("\"model_sample_rate\": 24000", "\"model_sample_rate\": 7999"), Contains("integer in [8000, 192000]")
  );
  CHECK(Edited("\"model_sample_rate\": 24000", "\"model_sample_rate\": 8000").sample_rate == 8000);
  CHECK_THROWS_WITH(
    Edited("\"embedding_dims\": [\n      5,", "\"embedding_dims\": [\n      257,"), Contains("integers in [1, 256]")
  );
}

TEST_CASE("vits voice config: keys that change the text have the one supported value", "[tts][vits]") {
  CHECK_THROWS_WITH(
    Edited("\"replace_excl_by_period\": false", "\"replace_excl_by_period\": true"),
    Contains("'replace_excl_by_period' = true is not supported, only false")
  );
  CHECK_THROWS_WITH(Edited("\"clean\"", "\"legacy\""), Contains("'pause_rules' = [\"punkt_to_pause\""));
  // The type of the model is a name of the package: any.
  CHECK_NOTHROW(Edited("\"model_type\": \"vits\"", "\"model_type\": \"other\""));
  CHECK_THROWS_WITH(
    Edited("\"use_quantization\": true", "\"use_quantization\": false"), Contains("'use_quantization'")
  );
  CHECK_THROWS_WITH(
    Edited("\"layer_num\": 1", "\"layer_num\": 2"), Contains("'layer_num' = 2 is not supported, only 1")
  );
  // Absent: the supported value. Unknown keys and keys of no effect here pass.
  CHECK_NOTHROW(Edited("\"replace_excl_by_period\": false,", ""));
  CHECK_NOTHROW(Edited("\"gain_db\": 17,", "\"gain_db\": 17, \"brand_new\": [1],"));
}

TEST_CASE("vits voice config: the number of speakers", "[tts][vits]") {
  CHECK(Edited("\"n_speakers\": 16", "\"n_speakers\": 2").speakers == 2);
  CHECK_FALSE(Edited("\"n_speakers\": 16,", "").speakers.has_value());
  CHECK_THROWS_WITH(Edited("\"n_speakers\": 16", "\"n_speakers\": 0"), Contains("'n_speakers'"));
  CHECK_THROWS_WITH(Edited("\"n_speakers\": 16", "\"n_speakers\": 2.5"), Contains("'n_speakers'"));
}

TEST_CASE("vits voice config: the pause threshold", "[tts][vits]") {
  const auto with = [](const std::string& value) {
    return Edited("\"pause_threshold\": 0.7", "\"pause_threshold\": " + value);
  };
  CHECK(with("0.2").pause_threshold == 0.2F);
  CHECK(with("0").pause_threshold == 0.0F);
  CHECK(with("1").pause_threshold == 1.0F);
  CHECK_THROWS_WITH(with("1.5"), Contains("'pause_threshold' must be in [0, 1]"));
  CHECK_THROWS_WITH(with("-0.1"), Contains("'pause_threshold' must be in [0, 1]"));
  CHECK_THROWS_WITH(with("\"0.5\""), Contains("'pause_threshold' must be a finite number"));
  CHECK(Edited("\"pause_threshold\"", "\"unused\"").pause_threshold == kDefaultPauseThreshold);
}

TEST_CASE("vits text ids: symbols between blanks", "[tts][vits]") {
  auto voice = test::TestVoice();
  // The ids of the reference.
  CHECK(
    EncodeText("включИл свЕт нА кУхне.", voice)
    == std::vector<int64_t>{0,  16, 0, 25, 0,  26, 0,  45, 0, 38, 0,  54, 0,  26, 0,  1, 0,  32, 0,  16, 0, 56, 0,
                            33, 0,  1, 0,  28, 0,  47, 0,  1, 0,  25, 0,  51, 0,  36, 0, 28, 0,  19, 0,  4, 0}
  );
  CHECK(EncodeText("", voice) == std::vector<int64_t>{0});
  CHECK(EncodeText("^Ё^^", voice) == std::vector<int64_t>{0, 57, 0, 50, 0, 57, 0, 57, 0});
  voice.blank_intersperse = false;
  CHECK(EncodeText("ёЁ?", voice) == std::vector<int64_t>{20, 50, 8});
  CHECK(InputLength(3, voice) == 3);
  voice.blank_intersperse = true;
  CHECK(InputLength(3, voice) == 7);

  CHECK_THROWS_WITH(EncodeText("свет x", voice), Contains("symbol 'x' at byte 9 is not in the voice alphabet"));
  CHECK_THROWS_WITH(EncodeText("свЕт«", voice), Contains("symbol '«' at byte 8"));
  CHECK_THROWS_WITH(EncodeText("св\xD0", voice), Contains("invalid UTF-8 at byte 4"));
}
