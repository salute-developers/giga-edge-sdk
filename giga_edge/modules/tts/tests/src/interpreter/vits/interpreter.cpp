#include "giga_edge/modules/tts/interpreter/backends/vits/interpreter.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/model/backends/onnxruntime/onnx_environment.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace giga_edge;
using Catch::Matchers::Contains;

// The whole backend on the synthetic models (tests/data/vits/synthetic) and the sample language data
// (tests/data/vits/text/ru); the noise is off, so a sentence always gives the same sound. The synthetic BERT knows
// only words of the letter 'а', and the synthetic models mark the labels of any other word as broken: the sentences
// here are made of such words.
namespace {

constexpr size_t kRate = 24000;
constexpr size_t kSilenceChunk = kRate / 5;

VitsTtsInterpreterParams Params(const std::string& more = "", const std::string& noise_w = "0") {
  return ParseConfig<VitsTtsInterpreterParams>(YAML::Load(
    "{models_path: vits/synthetic, text_path: vits/text/ru, voice: one, voices: {one: 3, two: 5}, noise_scale: 0,"
    " noise_scale_w: "
    + noise_w + more + "}"
  ));
}

std::unique_ptr<VitsTtsInterpreter> Make(const std::string& more = "", const std::string& noise_w = "0") {
  auto tts = std::make_unique<VitsTtsInterpreter>();
  REQUIRE(tts->Initialize(Params(more, noise_w), CreateDataReader("data/"), nullptr));
  REQUIRE(tts->IsReady());
  REQUIRE(tts->IsFinished());
  return tts;
}

bool IsSilence(const std::vector<float>& chunk) {
  return !chunk.empty() && std::all_of(chunk.begin(), chunk.end(), [](float sample) {
    return sample == 0.0F;
  });
}

struct Phrase {
  std::vector<float> audio;
  std::vector<std::vector<float>> chunks;
};

Phrase Speak(VitsTtsInterpreter& tts, const std::string& text) {
  Phrase phrase;
  REQUIRE(tts.StartSynthesis(text));
  while (!tts.IsFinished()) {
    REQUIRE(phrase.chunks.size() < 100'000);
    phrase.chunks.push_back(tts.GetAudioChunk());
    phrase.audio.insert(phrase.audio.end(), phrase.chunks.back().begin(), phrase.chunks.back().end());
  }
  REQUIRE(tts.GetAudioChunk().empty());
  tts.StopSynthesis();
  return phrase;
}

std::vector<float> Joined(const std::vector<float>& first, size_t silence, const std::vector<float>& second) {
  auto audio = first;
  audio.insert(audio.end(), silence, 0.0F);
  audio.insert(audio.end(), second.begin(), second.end());
  return audio;
}

} // namespace

TEST_CASE("VitsTtsInterpreter speaks sentences with the silence between them and none at the end", "[tts][vits]") {
  auto tts = Make();
  const auto hello = Speak(*tts, "а аа.").audio;
  const auto bye = Speak(*tts, "ааа.").audio;
  REQUIRE_FALSE(hello.empty());
  REQUIRE_FALSE(bye.empty());
  REQUIRE(hello != bye);
  REQUIRE(std::all_of(hello.begin(), hello.end(), [](float sample) {
    return std::isfinite(sample) && std::abs(sample) <= 1.0F;
  }));
  REQUIRE_FALSE(IsSilence(hello));
  // Not the clipped sound the synthetic models give for a sentence they take as broken.
  REQUIRE(*std::max_element(hello.begin(), hello.end()) < 1.0F);
  // The same sentence again: nothing of the previous phrase is left in the models.
  REQUIRE(Speak(*tts, "а аа.").audio == hello);

  const auto both = Speak(*tts, "а аа. ааа.");
  REQUIRE(both.audio == Joined(hello, kRate / 2, bye));
  // The silence comes in pieces of at most 200 ms, before the sentence it precedes.
  std::vector<size_t> silence;
  for (const auto& chunk : both.chunks) {
    if (IsSilence(chunk)) {
      silence.push_back(chunk.size());
    }
  }
  REQUIRE(silence == std::vector<size_t>{kSilenceChunk, kSilenceChunk, kRate / 2 - 2 * kSilenceChunk});

  REQUIRE(Speak(*Make(", sentence_pause_ms: 0"), "а аа. ааа.").audio == Joined(hello, 0, bye));
  // Every piece has its own pause: a long sentence cut at the comma takes the pause of a clause.
  const auto head = Speak(*tts, "ааа аа.").audio;
  const auto tail = Speak(*tts, "ааа аааа ааа аа.").audio;
  REQUIRE(
    Speak(*Make(", max_sentence_chars: 30"), "ааа аа, ааа аааа ааа аа.").audio == Joined(head, kRate * 150 / 1000, tail)
  );
  REQUIRE(Speak(*Make(", sentence_pause_ms: 10000"), "а аа. ааа.").audio == Joined(hello, 10 * kRate, bye));
  // A sentence without words is left out together with its pause.
  REQUIRE(Speak(*tts, "а аа. \U0001F600. ааа. ...").audio == Joined(hello, kRate / 2, bye));
  REQUIRE(Speak(*tts, "а аа. ...").audio == hello);
}

TEST_CASE("VitsTtsInterpreter gives no silence around a sentence without sound", "[tts][vits]") {
  // The synthetic full_text takes floor(noise_scale_w) frames off the sentence: all of them here. The synthetic
  // decoder makes 8 samples of a frame.
  const auto frames = Speak(*Make(), "а аа.").audio.size() / 8;
  auto mute = Make("", std::to_string(frames));
  const auto phrase = Speak(*mute, "а аа. а аа. а аа.");
  REQUIRE(phrase.audio.empty());
  REQUIRE(phrase.chunks.size() == 3);

  const auto short_frames = Speak(*Make(), "а.").audio.size() / 8;
  auto partly_mute = Make("", std::to_string(short_frames));
  const auto sound = Speak(*partly_mute, "а аа.").audio;
  REQUIRE_FALSE(sound.empty());
  REQUIRE(Speak(*partly_mute, "а.").audio.empty());
  REQUIRE(Speak(*partly_mute, "а. а аа.").audio == sound);
  REQUIRE(Speak(*partly_mute, "а аа. а.").audio == sound);
  REQUIRE(Speak(*partly_mute, "а аа. а. а аа.").audio == Joined(sound, kRate / 2, sound));
}

TEST_CASE("VitsTtsInterpreter does one unit of work in a call", "[tts][vits]") {
  auto tts = Make();
  const auto one = Speak(*tts, "а аа.");
  const auto three = Speak(*tts, "а аа. а аа. а аа.");
  // Per sentence: its steps of sound; between sentences: three pieces of silence.
  REQUIRE(three.chunks.size() == 3 * one.chunks.size() + 2 * 3);
  for (const auto& chunk : three.chunks) {
    REQUIRE_FALSE(chunk.empty());
  }
}

TEST_CASE("VitsTtsInterpreter takes text of any kind", "[tts][vits]") {
  auto tts = Make();
  const auto hello = Speak(*tts, "а аа.").audio;
  for (const std::string text : {"", " ", "?!", "!!!", "...", "—", "-", ".", "\U0001F600", "\xFF\xFE", "^", "_~*#`"}) {
    CAPTURE(text);
    REQUIRE(Speak(*tts, text).audio.empty());
  }
  REQUIRE(Speak(*tts, std::string("\0", 1)).audio.empty());
  // Bytes that start no symbol do not get around the limit of the text.
  REQUIRE(Speak(*Make(", max_text_chars: 1"), std::string(1'000'000, '\x80')).audio.empty());
  REQUIRE(Speak(*Make(", max_text_chars: 1"), std::string(100, '\x80') + "а.").audio.empty());
  // Markup and symbols outside the alphabet do not reach the sound.
  REQUIRE(Speak(*tts, "**а аа**.").audio == hello);
  REQUIRE(Speak(*tts, "_а аа_ \U0001F600.").audio == hello);
  REQUIRE(Speak(*tts, std::string("а аа\0.", 9)).audio == hello);
  // Long words and long texts are cut into sentences: the sound is there and the work is bounded.
  std::string letters;
  for (int i = 0; i < 10'000; ++i) {
    letters += "а";
  }
  REQUIRE_FALSE(Speak(*tts, letters).audio.empty());
  REQUIRE_FALSE(Speak(*tts, std::string(10'000, 'a')).audio.empty());
  std::string text;
  for (int i = 0; i < 200; ++i) {
    text += "а аа, ааа аааа. ";
  }
  const auto longest = Speak(*tts, text);
  REQUIRE_FALSE(longest.audio.empty());
  REQUIRE(Speak(*tts, "а аа.").audio == hello);
}

TEST_CASE("VitsTtsInterpreter drops the phrase on StopSynthesis", "[tts][vits]") {
  auto tts = Make();
  const auto hello = Speak(*tts, "а аа.").audio;
  const auto bye = Speak(*tts, "ааа.").audio;

  // In the middle of the sound.
  REQUIRE(tts->StartSynthesis("а аа. ааа."));
  REQUIRE_FALSE(tts->GetAudioChunk().empty());
  REQUIRE_FALSE(tts->IsFinished());
  tts->StopSynthesis();
  REQUIRE(tts->IsFinished());
  REQUIRE(tts->GetAudioChunk().empty());
  REQUIRE(Speak(*tts, "ааа.").audio == bye);

  // In the middle of the silence.
  REQUIRE(tts->StartSynthesis("а аа. ааа."));
  for (int chunk = 0; !IsSilence(tts->GetAudioChunk()); ++chunk) {
    REQUIRE(chunk < 100'000);
    REQUIRE_FALSE(tts->IsFinished());
  }
  tts->StopSynthesis();
  REQUIRE(tts->IsFinished());
  REQUIRE(Speak(*tts, "а аа. ааа.").audio == Joined(hello, kRate / 2, bye));

  // A new phrase drops the previous one by itself.
  REQUIRE(tts->StartSynthesis("а аа. ааа."));
  REQUIRE_FALSE(tts->GetAudioChunk().empty());
  REQUIRE(Speak(*tts, "ааа.").audio == bye);
  // StopSynthesis with no phrase.
  tts->StopSynthesis();
  tts->StopSynthesis();
  REQUIRE(Speak(*tts, "а аа.").audio == hello);
}

TEST_CASE("VitsTtsInterpreter changes the voice from the next phrase", "[tts][vits]") {
  auto tts = Make();
  const auto one = Speak(*tts, "а аа.").audio;
  REQUIRE(tts->SetVoice("two"));
  const auto two = Speak(*tts, "а аа.").audio;
  REQUIRE(two != one);
  REQUIRE(two.size() == one.size());

  // The phrase in synthesis keeps its voice.
  REQUIRE(tts->StartSynthesis("а аа. а аа."));
  std::vector<float> audio = tts->GetAudioChunk();
  REQUIRE(tts->SetVoice("one"));
  for (int step = 0; !tts->IsFinished(); ++step) {
    REQUIRE(step < 100'000);
    const auto chunk = tts->GetAudioChunk();
    audio.insert(audio.end(), chunk.begin(), chunk.end());
  }
  REQUIRE(audio == Joined(two, kRate / 2, two));
  REQUIRE(Speak(*tts, "а аа.").audio == one);

  // An unknown voice is refused: the configured one speaks; an empty name is the configured voice too.
  REQUIRE(tts->SetVoice("two"));
  REQUIRE_FALSE(tts->SetVoice("three"));
  REQUIRE(Speak(*tts, "а аа.").audio == one);
  REQUIRE(tts->SetVoice("two"));
  REQUIRE(tts->SetVoice(""));
  REQUIRE(Speak(*tts, "а аа.").audio == one);
}

TEST_CASE("VitsTtsInterpreter cuts a text longer than max_text_chars", "[tts][vits]") {
  auto tts = Make();
  const auto hello = Speak(*tts, "а аа.").audio;
  REQUIRE(Speak(*Make(", max_text_chars: 5"), "а аа. ааа.").audio == hello);
  REQUIRE(Speak(*Make(", max_text_chars: 4"), "а ааа. ааа.").audio == hello);
  REQUIRE(Speak(*Make(", max_text_chars: 10"), "а аа. ааа.").audio != hello);
}

TEST_CASE("VitsTtsInterpreter passes a failure of the models and works after StopSynthesis", "[tts][vits]") {
  // noise_scale_w 100 makes the synthetic full_text give NaN durations.
  auto broken = Make("", "100");
  REQUIRE(broken->StartSynthesis("а аа."));
  REQUIRE_THROWS(broken->GetAudioChunk());
  broken->StopSynthesis();
  REQUIRE(broken->IsFinished());
  REQUIRE(broken->StartSynthesis("а аа. ааа."));
  REQUIRE_THROWS(broken->GetAudioChunk());
  broken->StopSynthesis();
  REQUIRE(broken->GetAudioChunk().empty());
}

TEST_CASE("VitsTtsInterpreter passes its params to the parts", "[tts][vits]") {
  auto tts = Make();
  const auto hello = Speak(*tts, "а аа.").audio;
  // The synthetic durations are ceil(1 / speed) frames per symbol, with 8 samples per frame.
  REQUIRE(Speak(*Make(", speed: 0.4"), "аааааааааа.").audio.size() == 45 * 8);
  REQUIRE(Speak(*Make(", speed: 0.25"), "аааааааааа.").audio.size() == 68 * 8);
  REQUIRE(Speak(*Make(", speed: 2"), "аааааааааа.").audio.size() == 11 * 8);
  // A slow speech takes shorter sentences: the word is cut in two at speed 0.25 and stays whole at speed 1.
  std::string word;
  for (int i = 0; i < 300; ++i) {
    word += "а";
  }
  const auto silent_chunks = [&](const std::string& more) {
    const auto phrase = Speak(*Make(", max_sentence_chars: 400, word_pause_ms: 100" + more), word);
    return std::count_if(phrase.chunks.begin(), phrase.chunks.end(), IsSilence);
  };
  REQUIRE(silent_chunks("") == 0);
  REQUIRE(silent_chunks(", speed: 0.25") == 1);
  // A shared ONNX environment by its name.
  std::map<std::string, EnvironmentPtr> environments = {
    {"onnx_env", std::make_shared<OnnxEnvironment>(OnnxEnvironmentParams{})}
  };
  VitsTtsInterpreter shared;
  REQUIRE(shared.Initialize(Params(", onnx_environment: onnx_env"), CreateDataReader("data/"), &environments));
  REQUIRE(Speak(shared, "а аа.").audio == hello);
  VitsTtsInterpreter lost;
  REQUIRE_THROWS(lost.Initialize(Params(", onnx_environment: no_env"), CreateDataReader("data/"), &environments));
  // The text models request the environment before the acoustic params are checked.
  auto invalid = Params(", onnx_environment: no_env");
  invalid.acoustic.threads = 0;
  REQUIRE_THROWS_WITH(lost.Initialize(invalid, CreateDataReader("data/"), &environments), Contains("no_env"));
}

TEST_CASE("VitsTtsInterpreter bounds a sentence by the slowest symbols of the voice", "[tts][vits]") {
  const auto disk = CreateDataReader("data/");
  const auto voice = ToString(*disk->ReadTransientData("vits/synthetic/voice.json"));
  const std::string scale = "\"length_scale\": 1.0";
  REQUIRE(voice.find(scale) != std::string::npos);
  // The silences inside a word of `letters` letters: the places where the word is cut.
  const auto cuts = [&](const std::string& length_scale, const std::string& speed, size_t letters) {
    auto changed = voice;
    changed.replace(changed.find(scale), scale.size(), "\"length_scale\": " + length_scale);
    const auto reader = CreateCachingDataReader(
      disk, {{"vits/synthetic/voice.json", Create<IDataSpanPtr>(std::vector<char>(changed.begin(), changed.end()))}}
    );
    VitsTtsInterpreter tts;
    REQUIRE(tts.Initialize(Params(", max_sentence_chars: 400, word_pause_ms: 100, speed: " + speed), reader, nullptr));
    std::string word;
    for (size_t i = 0; i < letters; ++i) {
      word += "а";
    }
    const auto phrase = Speak(tts, word);
    return std::count_if(phrase.chunks.begin(), phrase.chunks.end(), IsSilence);
  };
  // A slower voice: its letters bound the sentence, 400·√(1 / 1.25) = 357 symbols at speed 1.
  REQUIRE(cuts("1.0", "1", 380) == 0);
  REQUIRE(cuts("1.25", "1", 380) == 1);
  // A faster voice: its blanks and marks are as slow as with 1.0, 400·√0.5 = 282 symbols at speed 0.5.
  REQUIRE(cuts("1.0", "0.5", 300) == 1);
  REQUIRE(cuts("0.8", "0.5", 300) == 1);
}

TEST_CASE("VitsTtsInterpreter reads Latin words by the dictionary and falls back to transliteration", "[tts][vits]") {
  auto params = Params();
  params.text_path = "vits/glue_english";
  VitsTtsInterpreter tts;
  REQUIRE(tts.Initialize(params, CreateDataReader("data/"), nullptr));
  // This dictionary reads 'aaa' as 'а'; it has no reading for 'aaaa'.
  const auto one = Speak(tts, "а.").audio;
  REQUIRE_FALSE(one.empty());
  REQUIRE_FALSE(IsSilence(one));
  REQUIRE(*std::max_element(one.begin(), one.end()) < 1.0F);
  const auto reading = Speak(tts, "aaa.").audio;
  REQUIRE(reading == one);
  REQUIRE(reading != Speak(tts, "ааа.").audio);
  REQUIRE(Speak(tts, "aaaa.").audio == Speak(tts, "аааа.").audio);
}

TEST_CASE("VitsTtsInterpreter tells words in capitals by the stress dictionary", "[tts][vits]") {
  VitsTtsInterpreter tts;
  REQUIRE(tts.Initialize(Params(), CreateDataReader("data/"), nullptr));
  // "время" is in the test stress dictionary, not among the words of words.tsv: in capitals among lower-case words it
  // is read as the word, not spelled as an abbreviation of two consonants at the start; one outside it is spelled.
  REQUIRE(Speak(tts, "он ВРЕМЯ.").audio == Speak(tts, "он время.").audio);
  REQUIRE(Speak(tts, "он ВТБ.").audio != Speak(tts, "он втб.").audio);
}

TEST_CASE(
  "VitsTtsInterpreter refuses speakers past the table of the voice and sentence params around ParseConfig",
  "[tts][vits]"
) {
  // The synthetic voice has 16 speakers (n_speakers of voice.json).
  VitsTtsInterpreter tts;
  auto voices = Params();
  voices.voices = {{"one", 3}, {"two", 15}};
  REQUIRE(tts.Initialize(voices, CreateDataReader("data/"), nullptr));
  voices.voices["two"] = 16;
  REQUIRE_THROWS_WITH(
    tts.Initialize(voices, CreateDataReader("data/"), nullptr),
    Contains("the speaker of voice 'two' is out of the table")
  );
  REQUIRE_FALSE(tts.IsReady());
  auto params = Params();
  params.sentences.max_sentence_chars = 10;
  REQUIRE_THROWS_AS(tts.Initialize(params, CreateDataReader("data/"), nullptr), std::invalid_argument);
}

TEST_CASE("VitsTtsInterpreter refuses a speed out of range before it is used", "[tts][vits]") {
  for (const float speed : {0.1F, 5.0F, std::numeric_limits<float>::quiet_NaN()}) {
    auto params = Params();
    params.acoustic.speed = speed;
    VitsTtsInterpreter tts;
    REQUIRE_THROWS_WITH(tts.Initialize(params, CreateDataReader("data/"), nullptr), Contains("'speed' must be in"));
    REQUIRE_FALSE(tts.IsReady());
    // Before anything is read.
    params.text_path = "vits/nothing";
    REQUIRE_THROWS_WITH(tts.Initialize(params, CreateDataReader("data/"), nullptr), Contains("'speed' must be in"));
  }
}

TEST_CASE("VitsTtsInterpreter checks the dictionaries when told to", "[tts][vits]") {
  const auto load = [](const std::string& text_path, const std::string& verify) {
    VitsTtsInterpreter tts;
    tts.Initialize(
      ParseConfig<VitsTtsInterpreterParams>(YAML::Load(
        "{models_path: vits/synthetic, voice: one, voices: {one: 3}, text_path: " + text_path
        + ", verify_dictionaries: " + verify + "}"
      )),
      CreateDataReader("data/"),
      nullptr
    );
  };
  // A byte of the dictionary is damaged: the check finds it; without the check the load goes on and stops at the
  // data these directories lack.
  REQUIRE_THROWS_WITH(load("vits/text_bad_stress", "true"), Contains("stress.dict"));
  REQUIRE_THROWS_WITH(load("vits/text_bad_stress", "false"), !Contains("stress.dict"));
  REQUIRE_THROWS_WITH(load("vits/text_bad_english", "true"), Contains("english.dict"));
  REQUIRE_THROWS_WITH(load("vits/text_bad_english", "false"), !Contains("english.dict"));
}

TEST_CASE("VitsTtsInterpreter is not ready before a load and after a failed one", "[tts][vits]") {
  VitsTtsInterpreter tts;
  const auto idle = [&tts] {
    REQUIRE_FALSE(tts.IsReady());
    REQUIRE(tts.IsFinished());
    REQUIRE_FALSE(tts.StartSynthesis("а аа."));
    REQUIRE(tts.GetAudioChunk().empty());
    REQUIRE_FALSE(tts.SetVoice("one"));
    tts.StopSynthesis();
  };
  idle();
  auto invalid = Params();
  invalid.voice = "missing";
  REQUIRE_THROWS_WITH(tts.Initialize(invalid, CreateDataReader("data/"), nullptr), Contains("no voice 'missing'"));
  idle();
  REQUIRE(tts.Initialize(Params(), CreateDataReader("data/"), nullptr));
  REQUIRE(tts.StartSynthesis("а аа. ааа."));
  REQUIRE_FALSE(tts.GetAudioChunk().empty());
  // A second load that fails leaves nothing of the first one.
  REQUIRE_THROWS(tts.Initialize(
    ParseConfig<VitsTtsInterpreterParams>(
      YAML::Load("{models_path: vits/synthetic, text_path: vits/nothing, voice: one, voices: {one: 3}}")
    ),
    CreateDataReader("data/"),
    nullptr
  ));
  idle();
  // And a load after it starts clean.
  REQUIRE(tts.Initialize(Params(), CreateDataReader("data/"), nullptr));
  REQUIRE(Speak(tts, "а аа.").audio == Speak(*Make(), "а аа.").audio);
}

TEST_CASE("VitsTtsInterpreter refuses a broken package", "[tts][vits]") {
  const auto with = [](const std::string& more) {
    return ParseConfig<VitsTtsInterpreterParams>(YAML::Load("{voice: one, voices: {one: 3}, " + more + "}"));
  };
  VitsTtsInterpreter tts;
  REQUIRE_THROWS_WITH(
    tts.Initialize(
      with("models_path: vits/voice_without_yo, text_path: vits/text/ru"), CreateDataReader("data/"), nullptr
    ),
    Contains("lacks a symbol")
  );
  REQUIRE_THROWS(
    tts.Initialize(with("models_path: vits/nothing, text_path: vits/text/ru"), CreateDataReader("data/"), nullptr)
  );
  REQUIRE_THROWS(
    tts.Initialize(with("models_path: vits/synthetic, text_path: vits/nothing"), CreateDataReader("data/"), nullptr)
  );
  REQUIRE_FALSE(tts.IsReady());
  REQUIRE(tts.IsFinished());
  REQUIRE_FALSE(tts.StartSynthesis("а аа."));
}
