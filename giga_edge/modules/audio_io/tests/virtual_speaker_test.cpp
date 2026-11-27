#include "giga_edge/modules/audio_io/speaker.h"

#include <catch.hpp>

#include <vector>

using namespace giga_edge;

TEST_CASE("VirtualSpeaker IsPlaying tracks fed vs played", "[audio_io][vspeaker]") {
  auto s = CreateVirtualSpeaker();
  size_t total = 0;
  s->SetPlaybackCallback([&](size_t n) {
    total += n;
  });
  CHECK_FALSE(s->IsPlaying());

  s->Play(std::vector<float>(1000));
  CHECK(s->IsPlaying());

  s->AdvancePlayed(600);
  CHECK(total == 600u);
  CHECK(s->IsPlaying());

  s->AdvancePlayed(600); // only 400 remain
  CHECK(total == 1000u); // callback clamps to queued
  CHECK_FALSE(s->IsPlaying());
}

TEST_CASE("VirtualSpeaker Stop drains without callback", "[audio_io][vspeaker]") {
  auto s = CreateVirtualSpeaker();
  size_t total = 0;
  s->SetPlaybackCallback([&](size_t n) {
    total += n;
  });
  s->Play(std::vector<float>(1000));
  s->Stop();
  CHECK_FALSE(s->IsPlaying());
  CHECK(total == 0u);
}
