#include "giga_edge/modules/audio_io/speaker.h"

#include <memory>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

class TeeSpeaker : public ISpeaker {
public:
  TeeSpeaker(ISpeaker& primary, ISpeaker& secondary)
    : primary_(primary)
    , secondary_(secondary) {}

  void Play(std::vector<float> audio) override {
    secondary_.Play(audio);
    primary_.Play(std::move(audio));
  }

  void Stop() override {
    primary_.Stop();
    secondary_.Stop();
  }

  bool IsPlaying() const override {
    return primary_.IsPlaying();
  }

  void SetPlaybackCallback(PlaybackCallback cb) override {
    primary_.SetPlaybackCallback(std::move(cb));
  }

private:
  ISpeaker& primary_;
  ISpeaker& secondary_;
};

} // namespace

std::shared_ptr<ISpeaker> CreateTeeSpeaker(ISpeaker& primary, ISpeaker& secondary) {
  return std::make_shared<TeeSpeaker>(primary, secondary);
}

} // namespace giga_edge
