#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class ISpeaker {
public:
  using PlaybackCallback = std::function<void(size_t samples_played)>;
  virtual ~ISpeaker() = default;
  virtual void Play(std::vector<float> audio) = 0;
  virtual void Stop() = 0;
  virtual bool IsPlaying() const = 0;
  virtual void SetPlaybackCallback(PlaybackCallback cb) = 0;
};

class IVirtualSpeaker : public ISpeaker {
public:
  virtual void AdvancePlayed(size_t n) = 0;
};

std::shared_ptr<ISpeaker> CreateMacosSpeaker(int sample_rate_hz, int channels);

std::shared_ptr<ISpeaker> CreateAplaySpeaker(int sample_rate_hz, int channels, std::string playback_device);

std::shared_ptr<IVirtualSpeaker> CreateVirtualSpeaker();

std::shared_ptr<ISpeaker> CreateTeeSpeaker(ISpeaker& primary, ISpeaker& secondary);

} // namespace giga_edge
