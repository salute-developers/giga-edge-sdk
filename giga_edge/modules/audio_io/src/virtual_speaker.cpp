#include "giga_edge/modules/audio_io/speaker.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

class VirtualSpeaker : public IVirtualSpeaker {
public:
  void Play(std::vector<float> audio) override {
    fed_ += audio.size();
  }

  void Stop() override {
    played_ = fed_;
  }

  bool IsPlaying() const override {
    return fed_ > played_;
  }

  void SetPlaybackCallback(PlaybackCallback cb) override {
    cb_ = std::move(cb);
  }

  void AdvancePlayed(size_t n) override {
    const size_t queued = fed_ - played_;
    const size_t done = std::min(n, queued);
    if (done == 0) {
      return;
    }
    played_ += done;
    if (cb_) {
      cb_(done);
    }
  }

private:
  size_t fed_ = 0;
  size_t played_ = 0;
  PlaybackCallback cb_;
};

} // namespace

std::shared_ptr<IVirtualSpeaker> CreateVirtualSpeaker() {
  return std::make_shared<VirtualSpeaker>();
}

} // namespace giga_edge
