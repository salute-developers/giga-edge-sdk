#include "giga_edge/lib/audio/audio.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

std::vector<float> Range(int start, int end) {
  std::vector<float> result;
  result.reserve(end - start);
  for (auto i = start; i < end; ++i) {
    result.push_back(i);
  }
  return result;
}

} // namespace

TEST_CASE("Test chunk_wav, single channel", "[audio]") {
  REQUIRE(ChunkWav(Range(0, 90), 30) == (std::vector<std::vector<float>>{Range(0, 30), Range(30, 60), Range(60, 90)}));

  REQUIRE(
    ChunkWav(Range(0, 100), 30)
    == (std::vector<std::vector<float>>{Range(0, 30), Range(30, 60), Range(60, 90), Range(90, 100)})
  );

  REQUIRE(ChunkWav(Range(0, 1200), 16000, 50) == (std::vector<std::vector<float>>{Range(0, 800), Range(800, 1200)}));
}

TEST_CASE("Test chunk_wav, multiple channels", "[audio]") {
  REQUIRE(
    ChunkWav(ChanneledAudio{Range(0, 100), Range(200, 300), Range(400, 500)}, 30)
    == (std::vector<ChanneledAudio>{
      {Range(0, 30), Range(200, 230), Range(400, 430)},
      {Range(30, 60), Range(230, 260), Range(430, 460)},
      {Range(60, 90), Range(260, 290), Range(460, 490)},
      {Range(90, 100), Range(290, 300), Range(490, 500)},
    })
  );

  REQUIRE(
    ChunkWav(ChanneledAudio{Range(0, 1200), Range(2000, 3200), Range(4000, 5200)}, 16000, 50)
    == (std::vector<ChanneledAudio>{
      {Range(0, 800), Range(2000, 2800), Range(4000, 4800)},
      {Range(800, 1200), Range(2800, 3200), Range(4800, 5200)},
    })
  );
}

TEST_CASE("Test chunk_wav, interleave audio", "[audio]") {
  ChanneledAudio audio{
    Range(0, 50),
    Range(100, 150),
    Range(200, 250),
    Range(300, 350),
    Range(400, 450),
  };

  std::vector<float> interleaved_audio(250);
  for (int i = 0; i < 250; ++i) {
    const int sample = i % 5 * 100 + i / 5;
    interleaved_audio[i] = static_cast<float>(sample);
  }

  REQUIRE(InterleaveAudio(audio) == interleaved_audio);

  REQUIRE(DeinterleaveAudio(interleaved_audio, 5) == audio);

  std::vector<float> interleaved_audio_3ch(150);
  for (int i = 0; i < 150; ++i) {
    const int sample = i % 3 * 200 + i / 3;
    interleaved_audio_3ch[i] = static_cast<float>(sample);
  }

  REQUIRE(InterleaveAudio(audio, {0, 2, 4}) == interleaved_audio_3ch);

  REQUIRE(DeinterleaveAudio(interleaved_audio_3ch, 3) == (ChanneledAudio{audio[0], audio[2], audio[4]}));
}

TEST_CASE("Interleave audio, no channels", "[audio]") {
  REQUIRE(InterleaveAudio({}, {0, 1, 2, 3}).empty());
  REQUIRE(InterleaveAudio({}).empty());
}