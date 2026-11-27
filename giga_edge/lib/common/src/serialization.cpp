#include "giga_edge/lib/common/serialization.h"

#include "giga_edge/lib/common/binary_io.h"
#include "giga_edge/lib/common/hash_fnv1a.h"

#include <iostream>
#include <sstream>

namespace giga_edge {

std::optional<std::vector<char>> SerializeRawAudioBinary(const std::vector<std::vector<float>>& raw_audio) {
  if (raw_audio.empty()) {
    return std::vector<char>{};
  }

  const size_t frames_per_channel = raw_audio.front().size();
  for (size_t channel_idx = 1; channel_idx < raw_audio.size(); ++channel_idx) {
    if (raw_audio[channel_idx].size() != frames_per_channel) {
      std::cerr << "SerializeRawAudioBinary: channel " << channel_idx << " has " << raw_audio[channel_idx].size()
                << " frames, expected " << frames_per_channel << std::endl;
      return std::nullopt;
    }
  }

  std::ostringstream stream(std::ios::out | std::ios::binary);
  WriteBinary(&stream, raw_audio);

  const std::string bytes = stream.str();
  return std::vector<char>(bytes.begin(), bytes.end());
}

std::vector<std::vector<float>> DeserializeRawAudioBinary(const std::vector<char>& payload) {
  std::istringstream stream(std::string(payload.begin(), payload.end()), std::ios::in | std::ios::binary);
  std::vector<std::vector<float>> decoded;
  giga_edge::ReadBinary(&stream, decoded);
  return decoded;
}

std::string ComputeHash64(const std::vector<char>& bytes) {
  return std::to_string(HashRangeFNV1a<uint64_t>(bytes.begin(), bytes.end()));
}

} // namespace giga_edge