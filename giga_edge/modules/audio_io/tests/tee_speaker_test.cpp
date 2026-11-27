#include "giga_edge/modules/audio_io/speaker.h"

#include <catch.hpp>

#include <vector>

using namespace giga_edge;

TEST_CASE("TeeSpeaker fans Play/Stop to both sinks", "[audio_io][tee]") {
  auto primary = CreateVirtualSpeaker();
  auto secondary = CreateVirtualSpeaker();
  auto tee = CreateTeeSpeaker(*primary, *secondary);

  tee->Play(std::vector<float>(500));
  CHECK(primary->IsPlaying());
  CHECK(secondary->IsPlaying());
  CHECK(tee->IsPlaying()); // reports from primary

  tee->Stop();
  CHECK_FALSE(primary->IsPlaying());
  CHECK_FALSE(secondary->IsPlaying());
}

TEST_CASE("TeeSpeaker routes playback callback to primary", "[audio_io][tee]") {
  auto primary = CreateVirtualSpeaker();
  auto secondary = CreateVirtualSpeaker();
  auto tee = CreateTeeSpeaker(*primary, *secondary);

  size_t played = 0;
  tee->SetPlaybackCallback([&](size_t n) {
    played += n;
  });
  tee->Play(std::vector<float>(800));
  primary->AdvancePlayed(800); // callback was installed on primary
  CHECK(played == 800u);
}

TEST_CASE("TeeSpeaker does not route callback to secondary", "[audio_io][tee]") {
  auto primary = CreateVirtualSpeaker();
  auto secondary = CreateVirtualSpeaker();
  auto tee = CreateTeeSpeaker(*primary, *secondary);

  size_t played = 0;
  tee->SetPlaybackCallback([&](size_t n) {
    played += n;
  });
  tee->Play(std::vector<float>(800));
  secondary->AdvancePlayed(800); // secondary has no callback installed
  CHECK(played == 0u);
}
