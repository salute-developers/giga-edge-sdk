#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/acoustic_model.h"

#include "helpers.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/model/backends/onnxruntime/onnx_environment.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;
using Catch::Matchers::Contains;

// The synthetic models of tests/data/vits/synthetic (see make_models.py there) under the voice config of the tests:
// num_frames 32, flow_overlap 23, decoder_overlap 13, so a decoder step is 19 frames.
namespace {

constexpr size_t kHop = 8;
constexpr size_t kStep = 19;
constexpr int64_t kSpeaker = 3;
constexpr float kNoise = 0.2F; // noise_scale of the voice config

AcousticParams Synthetic() {
  AcousticParams params;
  params.models_path = "vits/synthetic";
  params.gain_db = 3.0F; // the voice has 17 dB against the reference 20 dB: the gain is 1
  return params;
}

PreparedSentence Letters(size_t count, const std::string& tail = "") {
  std::string text;
  for (size_t i = 0; i < count; ++i) {
    text += "а";
  }
  return test::MakeSentence(text + tail, text + tail);
}

// A sample of output frame s as the synthetic models make it from a spectrogram of `frames` frames.
float Sample(size_t s, size_t frames, float noise, int64_t speaker) {
  const auto spk = static_cast<float>(speaker);
  // The flow call that made frame f: the first one makes frames 0..31, the next ones 19 frames each, if they start
  // inside the spectrogram. Past the spectrogram a frame is flow padding: only the flow term.
  const auto call = [](size_t f) {
    return f < 32 ? 0 : 1 + (f - 32) / kStep;
  };
  const auto z = [&](size_t f) {
    return f < frames ? static_cast<float>(f + 1) * 1e-4F + noise : 0.0F;
  };
  // Every frame of a flow call carries the last frame of its window: 55 frames from 0, then 42 from 32 + 19 (k - 1).
  const auto far = [&](size_t k) {
    return z(k == 0 ? 54 : 32 + kStep * (k - 1) + 41);
  };
  const auto value = [&](size_t f) {
    return z(f) + 0.01F * static_cast<float>(call(f)) + 1e-3F * far(call(f)) + 1e-4F * spk;
  };
  // The frames in the decoder window of step d: 32 from the first flow call, 19 from each later call that starts inside
  // the spectrogram, 19 gone with every decoder step; then the next frame of s in it (zero past them).
  const auto step = static_cast<long>(s / kStep);
  long calls = 0;
  for (long k = 1; k <= step; ++k) {
    calls += 32 + 19 * (k - 1) < static_cast<long>(frames) ? 1 : 0;
  }
  const auto count = std::clamp(32 + 19 * calls - 19 * step, 0L, 32L);
  const float next = static_cast<long>(s) - 19 * step + 1 < count ? value(s + 1) : 0.0F;
  return value(s) + 0.1F * static_cast<float>(step) + spk * 1e-3F + 1e-3F * next;
}

std::vector<std::vector<float>> Run(
  AcousticModel& model, const PreparedSentence& sentence, int64_t speaker = kSpeaker
) {
  model.Start(sentence, speaker);
  std::vector<std::vector<float>> chunks;
  while (!model.Finished()) {
    chunks.push_back(model.NextChunk());
  }
  CHECK(model.NextChunk().empty());
  return chunks;
}

// The audio of a spectrogram of `frames` frames, `audio` of them heard (all by default).
void CheckAudio(
  const std::vector<std::vector<float>>& chunks,
  size_t frames,
  float noise = kNoise,
  float gain = 1.0F,
  size_t audio = 0,
  int64_t speaker = kSpeaker
) {
  audio = audio == 0 ? frames : audio;
  INFO("frames " << frames << ", audio " << audio);
  REQUIRE(chunks.size() == (audio + kStep - 1) / kStep);
  size_t wrong = 0;
  std::string first_wrong;
  for (size_t chunk = 0; chunk < chunks.size(); ++chunk) {
    REQUIRE(chunks[chunk].size() == std::min(kStep, audio - kStep * chunk) * kHop);
    for (size_t i = 0; i < chunks[chunk].size(); ++i) {
      const auto frame = kStep * chunk + i / kHop;
      const auto expected = std::clamp(Sample(frame, frames, noise, speaker) * gain, -1.0F, 1.0F);
      if (!(std::abs(chunks[chunk][i] - expected) <= 1e-5F)) { // NaN is wrong too
        if (wrong++ == 0) {
          first_wrong = "frame " + std::to_string(frame) + ": " + std::to_string(chunks[chunk][i]) + " instead of "
            + std::to_string(expected);
        }
      }
    }
  }
  INFO(first_wrong);
  CHECK(wrong == 0);
}

} // namespace

TEST_CASE("vits acoustic model: streaming windows, states and the tail", "[tts][vits]") {
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  CHECK(model.SampleRate() == 24000);
  CHECK(model.Finished());
  CHECK(model.NextChunk().empty());
  // The same model sentence after sentence: every Start begins from zero states.
  for (const size_t frames : {1, 19, 32, 33, 51, 55, 56, 150, 20}) {
    CheckAudio(Run(model, Letters(frames)), frames);
  }
  // Marks and pauses in a sentence are frames too.
  CheckAudio(Run(model, test::MakeSentence("аа.", "^аА^.")), 5);
}

TEST_CASE("vits acoustic model: Reset, a new Start and errors in the middle", "[tts][vits]") {
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  model.Start(Letters(60), kSpeaker);
  CHECK_FALSE(model.NextChunk().empty());
  CHECK_FALSE(model.NextChunk().empty());
  model.Reset();
  CHECK(model.Finished());
  CHECK(model.NextChunk().empty());
  CheckAudio(Run(model, Letters(60)), 60);

  // Start in the middle of a sentence drops it.
  model.Start(Letters(60), kSpeaker);
  model.NextChunk();
  CheckAudio(Run(model, Letters(40)), 40);

  // A broken sentence throws and leaves the model ready.
  model.Start(Letters(60), kSpeaker);
  model.NextChunk();
  CHECK_THROWS_WITH(model.Start(test::MakeSentence("а x", "а x"), kSpeaker), Contains("not in the voice alphabet"));
  CHECK(model.Finished());
  CHECK_THROWS_WITH(model.Start({"", "а", {}}, kSpeaker), Contains("has no tokens"));
  CHECK_THROWS_WITH(model.Start({"а", "а", {}}, kSpeaker), Contains("outside the words"));
  CheckAudio(Run(model, Letters(33)), 33);

  // Nothing to say: no sentence, or no letters in it.
  for (const auto& silent :
       {PreparedSentence{}, PreparedSentence{"?", "?", {}}, PreparedSentence{", .", "^, ^.", {}}}) {
    model.Start(silent, kSpeaker);
    CHECK(model.Finished());
    CHECK(model.NextChunk().empty());
  }
  // Too long a sentence.
  std::string symbols(AcousticModel::kMaxSymbols + 1, '.');
  CHECK_THROWS_WITH(model.Start({symbols, symbols, {}}, kSpeaker), Contains("more than 800 symbols"));
  symbols.pop_back();
  model.Start({symbols, symbols, {}}, kSpeaker);
  CHECK(model.Finished());

  // The decoder fails on its third run for speaker 7: the model is reset, the next sentence is the one of a new model.
  AcousticModel fresh(Synthetic(), CreateDataReader("data/"));
  model.Start(Letters(60), 7);
  CHECK(model.NextChunk().size() == kStep * kHop);
  CHECK(model.NextChunk().size() == kStep * kHop);
  CHECK_THROWS_WITH(model.NextChunk(), Contains("Gather"));
  CHECK(model.Finished());
  CHECK(model.NextChunk().empty());
  CHECK(Run(model, Letters(60)) == Run(fresh, Letters(60)));
  CheckAudio(Run(model, Letters(60)), 60);
}

TEST_CASE("vits acoustic model: a long sentence through BERT windows, prosody and sound", "[tts][vits]") {
  // Letters and marks in turn: 800 symbols, the most a sentence may have, and 800 BERT tokens in two windows.
  std::string text;
  for (int i = 0; i < 400; ++i) {
    text += "а.";
  }
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  CheckAudio(Run(model, test::MakeSentence(text, text)), 800);
}

TEST_CASE("vits acoustic model: a shared ONNX environment", "[tts][vits]") {
  const std::map<std::string, EnvironmentPtr> environments{
    {"onnx_env", std::make_shared<OnnxEnvironment>(OnnxEnvironmentParams{})}
  };
  auto params = Synthetic();
  params.onnx_environment = "onnx_env";
  AcousticModel shared(params, CreateDataReader("data/"), &environments);
  AcousticModel own(Synthetic(), CreateDataReader("data/"));
  CHECK(Run(shared, Letters(60)) == Run(own, Letters(60)));
  params.onnx_environment = "missing";
  CHECK_THROWS_WITH(AcousticModel(params, CreateDataReader("data/"), &environments), Contains("missing"));
}

TEST_CASE("vits acoustic model: broken durations throw before anything is allocated by them", "[tts][vits]") {
  // floor(noise_scale_w) of the synthetic full_text: 100 NaN, 101 +Inf, 102 negative, 103 huge durations, 104 huge
  // y_lengths; 50 gives a negative y_lengths.
  const std::vector<std::pair<float, std::string>> cases = {
    {100.0F, "not a finite number >= 0"},
    {101.0F, "not a finite number >= 0"},
    {102.0F, "not a finite number >= 0"},
    {103.0F, "more than 100 frames per symbol"},
    {104.0F, "more than 100 frames per symbol"},
    {50.0F, "a negative number of frames"},
  };
  for (const auto& [mode, message] : cases) {
    auto params = Synthetic();
    params.noise_scale_w = mode;
    AcousticModel model(params, CreateDataReader("data/"));
    INFO(mode);
    CHECK_THROWS_WITH(model.Start(Letters(40), kSpeaker), Contains(message));
    CHECK(model.Finished());
  }
}

TEST_CASE("vits acoustic model: the speaker of a sentence stays to its end", "[tts][vits]") {
  // The speaker is set in Start only: sentence after sentence each keeps its own.
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  model.Start(Letters(60), 5);
  const auto first = model.NextChunk();
  auto other = AcousticModel(Synthetic(), CreateDataReader("data/"));
  const auto five = Run(other, Letters(60), 5);
  CHECK(first == five[0]);
  std::vector<std::vector<float>> rest{first};
  while (!model.Finished()) {
    rest.push_back(model.NextChunk());
  }
  CHECK(rest == five);
  CheckAudio(five, 60, kNoise, 1.0F, 0, 5);
  CHECK(Run(model, Letters(60), 3) != five);
}

TEST_CASE("vits acoustic model: speed, noise, gain and the output range", "[tts][vits]") {
  auto params = Synthetic();
  // The speed scales letters, marks and blanks alike: 11 symbols of ceil(length_scale) frames, 12 blanks of
  // floor(length_scale / 2).
  params.speed = 0.4F; // 11 x 3 + 12 x 1
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(10, ".")), 45);
  params.speed = 0.25F; // 11 x 4 + 12 x 2
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(10, ".")), 68);
  params.speed = 2.0F; // 11 x 1 + 12 x 0
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(10, ".")), 11);

  params = Synthetic();
  params.noise_scale = 0.0F;
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(40)), 40, 0.0F);
  params.noise_scale = 1.5F; // above 1: limited
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(40)), 40, 1.5F);

  // A spectrogram shorter than the durations (the synthetic y_lengths drops floor(noise_scale_w)): the audio ends with it.
  params = Synthetic();
  params.noise_scale_w = 3.0F;
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(40)), 37);

  params = Synthetic();
  params.gain_db = 3.0F - 20.0F * std::log10(2.0F);
  CheckAudio(Run(*std::make_unique<AcousticModel>(params, CreateDataReader("data/")), Letters(40)), 40, kNoise, 0.5F);

  // Speaker 12 makes samples of 1e37 or so: infinite after the gain of +40 dB, silence.
  params = Synthetic();
  params.gain_db = AcousticParams::kMaxGainDb;
  AcousticModel loud(params, CreateDataReader("data/"));
  for (const auto& chunk : Run(loud, Letters(25), 12)) {
    CHECK(std::all_of(chunk.begin(), chunk.end(), [](float sample) {
      return sample == 0.0F;
    }));
  }

  // Speaker 11 negates the audio: limited at -1.
  params = Synthetic();
  params.noise_scale = 1.5F;
  AcousticModel negative(params, CreateDataReader("data/"));
  for (const auto& chunk : Run(negative, Letters(25), 11)) {
    CHECK(std::all_of(chunk.begin(), chunk.end(), [](float sample) {
      return sample == -1.0F;
    }));
  }

  // The synthetic decoder gives NaN for speaker 0 and +Inf for speaker 1: silence.
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  for (const int64_t speaker : {0, 1}) {
    const auto chunks = Run(model, Letters(25), speaker);
    REQUIRE(chunks.size() == 2);
    for (const auto& chunk : chunks) {
      CHECK(std::all_of(chunk.begin(), chunk.end(), [](float sample) {
        return sample == 0.0F;
      }));
    }
  }
}

TEST_CASE("vits acoustic model: params", "[tts][vits]") {
  const auto defaults = ParseConfig<AcousticParams>(YAML::Load("{}"));
  CHECK(defaults.models_path.empty());
  CHECK(defaults.threads == 1);
  CHECK(defaults.speed == 1.0F);
  CHECK_FALSE(defaults.noise_scale.has_value());
  CHECK(defaults.gain_db == 0.0F);
  CHECK(defaults.optimization_level == OnnxOptimizationLevel::BASIC);
  CHECK_FALSE(defaults.local_debug_enabled);

  const auto params = ParseConfig<AcousticParams>(YAML::Load(
    "{models_path: m, onnx_environment: onnx, threads: 4, speed: 1.5, noise_scale: 0, noise_scale_w: 0.1, gain_db: -6, "
    "optimization_level: disable,"
    " local_debug_enabled: true}"
  ));
  CHECK(params.models_path == "m");
  CHECK(params.onnx_environment == "onnx");
  CHECK(params.threads == 4);
  CHECK(params.speed == 1.5F);
  CHECK(params.noise_scale == 0.0F);
  CHECK(params.noise_scale_w == 0.1F);
  CHECK(params.gain_db == -6.0F);
  CHECK(params.optimization_level == OnnxOptimizationLevel::DISABLE);
  CHECK(params.local_debug_enabled);
  CHECK_THROWS(ParseConfig<AcousticParams>(YAML::Load("{optimization_level: fastest}")));
  CHECK_THROWS(ParseConfig<AcousticParams>(YAML::Load("{threads: many}")));
  // Ranges are checked when parsed.
  CHECK(ParseConfig<AcousticParams>(YAML::Load("{speed: 0.25}")).speed == 0.25F);
  CHECK(ParseConfig<AcousticParams>(YAML::Load("{speed: 4}")).speed == 4.0F);
  for (const auto* bad :
       {"{speed: 0.24}",
        "{speed: 4.01}",
        "{speed: .nan}",
        "{threads: 0}",
        "{noise_scale: -0.1}",
        "{noise_scale_w: -1}",
        "{noise_scale: .inf}",
        "{gain_db: .inf}",
        "{gain_db: 40.5}",
        "{gain_db: -60.5}",
        "{gain_db: .nan}"}) {
    INFO(bad);
    CHECK_THROWS_AS(ParseConfig<AcousticParams>(YAML::Load(bad)), std::invalid_argument);
  }

  const auto reader = CreateDataReader("data/");
  const auto broken = [&reader](auto edit) {
    auto p = Synthetic();
    edit(p);
    return AcousticModel(p, reader);
  };
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.threads = 0;
    }),
    Contains("'threads' must be >= 1")
  );
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.speed = 0;
    }),
    Contains("'speed' must be in [0.25, 4]")
  );
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.speed = std::nanf("");
    }),
    Contains("'speed' must be in [0.25, 4]")
  );
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.noise_scale = -1.0F;
    }),
    Contains("'noise_scale' must be >= 0")
  );
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.noise_scale_w = -1.0F;
    }),
    Contains("'noise_scale_w' must be >= 0")
  );
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.gain_db = std::numeric_limits<float>::infinity();
    }),
    Contains("'gain_db' must be in [-60, 40]")
  );
  CHECK_THROWS(broken([](auto& p) {
    p.models_path = "vits/missing";
  }));
  // Another element type of a tensor the code writes: found at load, before any data.
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.models_path = "vits/synthetic_types";
    }),
    Contains("'text_ids' of text_qf0 has another element type")
  );
  // A flow whose state grows from run to run: found at load.
  CHECK_THROWS_WITH(
    broken([](auto& p) {
      p.models_path = "vits/synthetic_state";
    }),
    Contains("the state of flow changes its shape")
  );
  CHECK_THROWS(broken([](auto& p) {
    p.onnx_environment = "onnx";
  }));
}

TEST_CASE("vits BERT: windows of 511 tokens", "[tts][vits]") {
  const auto reader = CreateDataReader("data/");
  InterpreterParams params{};
  params.type = InterpreterParams::InterpreterType::ONNX;
  Bert bert(
    MakeInterpreter(params, "vits/synthetic/bert.onnx", reader),
    WordPiece(ToString(reader->ReadTransientData("vits/synthetic/bert_vocab.txt")))
  );
  CHECK_THROWS_WITH(
    Bert(
      MakeInterpreter(params, "vits/synthetic/bert_int32.onnx", reader),
      WordPiece(ToString(reader->ReadTransientData("vits/synthetic/bert_vocab.txt")))
    ),
    Contains("'token_ids' of bert has another element type")
  );
  const auto empty = bert.Embed("");
  CHECK(empty.tokens.empty());
  CHECK(empty.values.empty());

  for (const size_t count : {1, 511, 512, 1100}) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
      text += "а ";
    }
    const auto embeddings = bert.Embed(text + "?");
    REQUIRE(embeddings.tokens.size() == count + 1);
    REQUIRE(embeddings.size == 4);
    REQUIRE(embeddings.values.size() == (count + 1) * 4);
    size_t wrong = 0;
    for (size_t token = 0; token <= count; ++token) {
      const auto* row = embeddings.values.data() + token * 4;
      const float id = token < count ? 1.0F : 5.0F;
      wrong += row[0] == id && row[1] == static_cast<float>(token % Bert::kWindow) ? 0 : 1;
    }
    CHECK(wrong == 0);
  }
}

static_assert(!std::is_copy_constructible_v<AcousticModel> && !std::is_move_constructible_v<AcousticModel>);

TEST_CASE("vits acoustic model: broken outputs and inputs of the models", "[tts][vits]") {
  // full_text: y_lengths of shape [0] (105), w_ceil of one position more (106); a spectrogram longer than the
  // durations (200 + k): the audio is the durations.
  for (const float mode : {105.0F, 106.0F}) {
    auto params = Synthetic();
    params.noise_scale_w = mode;
    AcousticModel model(params, CreateDataReader("data/"));
    CHECK_THROWS_WITH(model.Start(Letters(40), kSpeaker), Contains("w_ceil or y_lengths of an unexpected shape"));
  }
  auto params = Synthetic();
  params.noise_scale_w = 205.0F;
  AcousticModel longer(params, CreateDataReader("data/"));
  CheckAudio(Run(longer, Letters(40)), 45, kNoise, 1.0F, 40);

  // flow gives a frame more (speaker 9), the decoder a sample more (speaker 10), BERT a row more (token "б").
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  model.Start(Letters(40), 9);
  CHECK_THROWS_WITH(model.NextChunk(), Contains("flow gives an output of an unexpected size"));
  model.Start(Letters(40), 10);
  CHECK_THROWS_WITH(model.NextChunk(), Contains("the decoder gives audio of an unexpected size"));
  CHECK_THROWS_WITH(
    model.Start(test::MakeSentence("б", "б"), kSpeaker), Contains("BERT encodings have an unexpected shape")
  );
  CheckAudio(Run(model, Letters(40)), 40);

  // A decoder of three inputs, as in the compressed release models.
  params = Synthetic();
  params.models_path = "vits/synthetic_compact";
  AcousticModel compact(params, CreateDataReader("data/"));
  CheckAudio(Run(compact, Letters(60)), 60);
  // A flow of one input more: a state nobody would pass.
  params.models_path = "vits/synthetic_inputs";
  CHECK_THROWS_WITH(AcousticModel(params, CreateDataReader("data/")), Contains("flow has an unknown input 'prev_aux'"));
  params.models_path = "vits/synthetic_bert_inputs";
  CHECK_THROWS_WITH(AcousticModel(params, CreateDataReader("data/")), Contains("bert has an unknown input 'prev_aux'"));

  // The strict full_text checks the labels in place: right for sentences of known letters.
  params.models_path = "vits/synthetic_strict";
  AcousticModel strict(params, CreateDataReader("data/"));
  CheckAudio(Run(strict, Letters(40)), 40);
  CheckAudio(Run(strict, Letters(10, ".")), 11);
}

TEST_CASE("vits acoustic model: long texts are refused before they are held", "[tts][vits]") {
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  const std::string plain(AcousticModel::kMaxPlainBytes + 1, 'a');
  CHECK_THROWS_WITH(model.Start({plain, "а", {}}, kSpeaker), Contains("plain text of more than 3200 bytes"));
  // A very long word is one [UNK]: its letters get no labels, the sentence is spoken in full; so is a question.
  std::string word;
  for (int i = 0; i < 700; ++i) {
    word += "а";
  }
  CheckAudio(Run(model, test::MakeSentence(word, word)), 700);
  CheckAudio(Run(model, test::MakeSentence("аа ааа?", "аА ааА?")), 7);
  // Frames by positions above the budget: 800 symbols with 5300 frames (y_lengths 4500 frames more, mode 4700) are
  // 5300 x 1601; 5200 frames still fit.
  std::string text;
  for (int i = 0; i < 400; ++i) {
    text += i == 0 ? "а" : " а";
  }
  text += ".";
  auto params = Synthetic();
  params.noise_scale_w = 4700.0F;
  AcousticModel slow(params, CreateDataReader("data/"));
  CHECK_THROWS_WITH(
    slow.Start(test::MakeSentence(text, text), kSpeaker), Contains("too long a sentence for its speed")
  );
  params.noise_scale_w = 4600.0F;
  AcousticModel fits(params, CreateDataReader("data/"));
  fits.Start(test::MakeSentence(text, text), kSpeaker);
  CHECK_FALSE(fits.Finished());
}

TEST_CASE("vits acoustic model: the speaker range and calls from other threads", "[tts][vits]") {
  AcousticModel model(Synthetic(), CreateDataReader("data/"));
  // A negative id would take a speaker from the end of the table; one past the table of 16 fails in the session.
  CHECK_THROWS_WITH(model.Start(Letters(30), -1), Contains("a negative speaker id -1"));
  CHECK(model.Finished());
  CHECK_THROWS_WITH(model.Start(Letters(30), 16), Contains("Gather"));
  CHECK(model.Finished());
  CheckAudio(Run(model, Letters(30), 15), 30, kNoise, 1.0F, 0, 15);

  // One call at a time from any thread: the same sound as from one thread.
  const auto expected = Run(model, Letters(60));
  std::vector<std::vector<float>> chunks;
  std::thread([&] {
    model.Start(Letters(60), kSpeaker);
  }).join();
  while (!model.Finished()) {
    std::thread([&] {
      chunks.push_back(model.NextChunk());
    }).join();
  }
  CHECK(chunks == expected);
}

TEST_CASE("vits acoustic model: models in the ORT format, copied or used in place", "[tts][vits]") {
  CHECK_FALSE(ParseConfig<AcousticParams>(YAML::Load("{}")).mmap_models);
  CHECK(ParseConfig<AcousticParams>(YAML::Load("{mmap_models: true}")).mmap_models);
  auto params = Synthetic();
  params.models_path = "vits/synthetic_ort";
  // The ORT format under the .onnx names: loaded either way, the same sound.
  AcousticModel copied(params, CreateDataReader("data/"));
  CheckAudio(Run(copied, Letters(60)), 60);
  params.mmap_models = true;
  AcousticModel mapped(params, CreateDataReader("data/"));
  CheckAudio(Run(mapped, Letters(60)), 60);
  CheckAudio(Run(mapped, Letters(10, ".")), 11);
  // In place needs the ORT format.
  params.models_path = "vits/synthetic";
  CHECK_THROWS_WITH(
    AcousticModel(params, CreateDataReader("data/")),
    Contains("'mmap_models' takes models in the ORT format, 'vits/synthetic/bert.onnx' is not")
  );
}

TEST_CASE("vits acoustic model: weights in files next to the models", "[tts][vits]") {
  // The weights of each model in <name>.onnx.data beside it, deeper than the root of the reader: the same sound.
  auto params = Synthetic();
  params.models_path = "vits/synthetic_external";
  AcousticModel model(params, CreateDataReader("data/"));
  CheckAudio(Run(model, Letters(60)), 60);
  CheckAudio(Run(model, Letters(10, ".")), 11);
}

// The synthetic pause model gives the token "##а" the softmax 0.62 and "а" 0.38 (make_models.py).
TEST_CASE("vits acoustic model: the pause model", "[tts][vits]") {
  auto params = Synthetic();
  params.models_path = "vits/synthetic_pauses";
  AcousticModel model(params, CreateDataReader("data/"));
  const auto paused = [&model](const std::string& text) {
    return model.AddPauses(test::MakeSentence(text, text)).marked;
  };
  // Above 0.5: after a word ending in "##а", when a letter follows; never before a mark, a pause or at the end.
  CHECK(paused("а аа а аа.") == "а аа^ а аа.");
  CHECK(paused("ааа а") == "ааа^ а");
  CHECK(paused("аа, а аа^ а аа") == "аа, а аа^ а аа");
  CHECK(paused("аа - а аа? аа") == "аа - а аа? аа");
  CHECK(paused("аа") == "аа");
  // The ranges of the words move with the pauses before them.
  const auto sentence = model.AddPauses(test::MakeSentence("аа аа а.", "аА аА А."));
  CHECK(sentence.marked == "аА^ аА^ А.");
  CHECK(sentence.plain == "аа аа а.");
  CHECK_NOTHROW(CheckWords(sentence));
  REQUIRE(sentence.words.size() == 3);
  CHECK(sentence.words[1].marked.begin == 6);
  CHECK(sentence.words[2].marked.begin == 12);
  // Only the last token of a word counts: "аа-а" is one word ending in "а".
  PreparedSentence hyphen{"аа-а аа а", "аа-а аа а", {{{0, 7}, {0, 7}}, {{8, 4}, {8, 4}}, {{13, 2}, {13, 2}}}};
  CHECK(model.AddPauses(hyphen).marked == "аа-а аа^ а");

  // The threshold of the voice: at 0.3 "а" gets pauses too.
  params.models_path = "vits/synthetic_pauses_low";
  AcousticModel low(params, CreateDataReader("data/"));
  CHECK(low.AddPauses(test::MakeSentence("а аа а", "а аа а")).marked == "а^ аа^ а");

  // The sound is that of the paused sentence through every model.
  params.models_path = "vits/synthetic";
  AcousticModel plain(params, CreateDataReader("data/"));
  CHECK(
    Run(model, test::MakeSentence("а аа а аа.", "а аа а аа."))
    == Run(plain, test::MakeSentence("а аа а аа.", "а аа^ а аа."))
  );
  // The strict full_text checks the labels of every symbol in place: prosody runs on the paused sentence.
  params.models_path = "vits/synthetic_pauses_strict";
  AcousticModel strict_paused(params, CreateDataReader("data/"));
  params.models_path = "vits/synthetic_strict";
  AcousticModel strict(params, CreateDataReader("data/"));
  CHECK(
    Run(strict_paused, test::MakeSentence("а аа а аа.", "а аа а аа."))
    == Run(strict, test::MakeSentence("а аа а аа.", "а аа^ а аа."))
  );
  params.models_path = "vits/synthetic";
  // Without the model the text stays.
  CHECK(plain.AddPauses(test::MakeSentence("а аа а аа.", "а аа а аа.")).marked == "а аа а аа.");

  // A broken model: an unknown input at load, other scores at use.
  params.models_path = "vits/synthetic_pauses_inputs";
  CHECK_THROWS_WITH(
    AcousticModel(params, CreateDataReader("data/")), Contains("pause_predictor has an unknown input 'lengths'")
  );
  params.models_path = "vits/synthetic_pauses_shape";
  AcousticModel shape(params, CreateDataReader("data/"));
  CHECK_THROWS_WITH(
    shape.AddPauses(test::MakeSentence("а аа а", "а аа а")), Contains("pause_scores of the pause model are not")
  );
  CHECK(shape.AddPauses(test::MakeSentence("аа", "аа")).marked == "аа");
}

TEST_CASE("vits acoustic model: local debug output", "[tts][vits]") {
  auto params = Synthetic();
  const auto run = [&params](const PreparedSentence& sentence) {
    AcousticModel model(params, CreateDataReader("data/"));
    std::ostringstream log;
    // Restored even when Run throws.
    const std::unique_ptr<std::streambuf, void (*)(std::streambuf*)> saved(
      std::cerr.rdbuf(log.rdbuf()), [](auto* buffer) {
        std::cerr.rdbuf(buffer);
      }
    );
    const auto chunks = Run(model, sentence);
    CHECK(chunks.empty() == sentence.marked.empty());
    return log.str();
  };
  CHECK(run(Letters(40)).empty());
  params.local_debug_enabled = true;
  const auto log = run(Letters(40));
  INFO(log);
  CHECK_THAT(log, Contains("[vits acoustic] plain \"" + Letters(40).plain + "\""));
  CHECK_THAT(log, Contains("[vits acoustic] ms: bert "));
  CHECK_THAT(log, Contains("flow ") && Contains(" in 2 steps, decoder ") && Contains(" in 3 steps"));
  CHECK_THAT(run({}), Contains("in 0 steps, decoder ") && Contains(" in 0 steps"));
}
