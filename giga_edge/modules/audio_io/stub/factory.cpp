#include "giga_edge/lib/common/not_supported.h"
#include "giga_edge/modules/audio_io/microphone.h"
#include "giga_edge/modules/audio_io/speaker.h"

#include <memory>
#include <string>

namespace giga_edge {

namespace {
constexpr const char* kModuleName = "audio_io";
} // namespace

std::shared_ptr<IMicrophone> CreateMacosMicrophone(
  int /*sample_rate_hz*/, int /*chunk_duration_ms*/, IMicrophone::OnChunk /*on_chunk*/
) {
  throw NotSupported(kModuleName);
}

std::shared_ptr<IMicrophone> CreateArecordMicrophone(
  int /*sample_rate_hz*/,
  int /*chunk_duration_ms*/,
  std::string /*capture_device*/,
  IMicrophone::OnChunk /*on_chunk*/,
  IMicrophone::OnFinished /*on_finished*/
) {
  throw NotSupported(kModuleName);
}

std::shared_ptr<ISpeaker> CreateMacosSpeaker(int /*sample_rate_hz*/, int /*channels*/) {
  throw NotSupported(kModuleName);
}

std::shared_ptr<ISpeaker> CreateAplaySpeaker(int /*sample_rate_hz*/, int /*channels*/, std::string /*device*/) {
  throw NotSupported(kModuleName);
}

std::shared_ptr<IVirtualSpeaker> CreateVirtualSpeaker() {
  throw NotSupported(kModuleName);
}

std::shared_ptr<ISpeaker> CreateTeeSpeaker(ISpeaker& /*primary*/, ISpeaker& /*secondary*/) {
  throw NotSupported(kModuleName);
}

} // namespace giga_edge
