#include "giga_edge/lib/audio/audio.h"

#include "AudioFile.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace giga_edge {

std::vector<float> LoadWav(const std::string& path) {
  AudioFile<float> audio_file;
  if (!audio_file.load(path)) {
    throw std::runtime_error("Cannot load wav " + path);
  }
  const auto& audio_buffer = audio_file.samples[0];
  return audio_buffer;
}

std::vector<float> LoadWav(std::vector<uint8_t>& data) {
  AudioFile<float> audio_file;
  if (!audio_file.loadFromMemory(data)) {
    throw std::runtime_error("Cannot load wav from memory");
  }
  const auto& audio_buffer = audio_file.samples[0];
  return audio_buffer;
}

std::vector<float> LoadWavFromContent(const std::string& content) {
  std::vector<float> wav_data;

  auto buffer = reinterpret_cast<const float*>(content.c_str());
  wav_data.reserve(content.size() / 4);

  for (size_t i = 0; i < (content.size() / 4); ++i) {
    wav_data.push_back(buffer[i]);
  }
  return wav_data;
}

std::vector<std::vector<float>> ChunkWav(const std::vector<float>& wav, int chunk_size) {
  if (chunk_size <= 0) {
    throw std::invalid_argument("ChunkWav: chunk_size should be > 0, but is " + std::to_string(chunk_size));
  }
  std::vector<std::vector<float>> chunks;
  if (chunk_size > wav.size()) {
    return {wav};
  }

  size_t num_chunks = (wav.size() + chunk_size - 1) / chunk_size;
  chunks.reserve(num_chunks);
  for (size_t step = 0; step < num_chunks; ++step) {
    chunks.emplace_back(wav.begin() + step * chunk_size, wav.begin() + std::min((step + 1) * chunk_size, wav.size()));
  }
  return chunks;
}

std::vector<std::vector<float>> ChunkWav(const std::vector<float>& wav, int sample_rate, int chunk_size_ms) {
  return ChunkWav(wav, (sample_rate * chunk_size_ms) / 1000);
}

std::vector<ChanneledAudio> ChunkWav(const ChanneledAudio& audio, int chunk_size) {
  std::vector<ChanneledAudio> result;

  for (int channel_idx = 0; channel_idx < audio.size(); ++channel_idx) {
    auto chunks = ChunkWav(audio[channel_idx], chunk_size);
    if (result.empty()) {
      result.assign(chunks.size(), ChanneledAudio(audio.size()));
    }
    for (int chunk_idx = 0; chunk_idx < chunks.size(); ++chunk_idx) {
      result[chunk_idx][channel_idx] = std::move(chunks[chunk_idx]);
    }
  }
  return result;
}

std::vector<ChanneledAudio> ChunkWav(const ChanneledAudio& wav, int sample_rate, int chunk_size_ms) {
  return ChunkWav(wav, (sample_rate * chunk_size_ms) / 1000);
}

std::vector<float> InterleaveAudio(
  const std::vector<std::vector<float>>& audio, const std::vector<int>& channel_indices
) {
  if (audio.empty()) {
    return {};
  }

  if (channel_indices.empty()) {
    std::vector<int> indices_all;
    indices_all.reserve(audio.size());
    for (auto i = 0; i < audio.size(); ++i) {
      indices_all.push_back(i);
    }
    return InterleaveAudio(audio, indices_all);
  }

  std::vector<float> result;
  result.reserve(channel_indices.size() * audio[0].size());
  for (auto i = 0; i < audio[0].size(); ++i) {
    for (auto channel_id : channel_indices) {
      result.push_back(audio[channel_id][i]);
    }
  }
  return result;
}

ChanneledAudio DeinterleaveAudio(const std::vector<float>& audio, int num_channels) {
  if (num_channels <= 0) {
    throw std::invalid_argument(
      "DeinterleaveAudio: num_channels should be > 0, but is " + std::to_string(num_channels)
    );
  }
  ChanneledAudio result;
  result.resize(num_channels);

  for (auto& channel : result) {
    channel.reserve(audio.size() / num_channels);
  }

  for (int i = 0; i < audio.size(); ++i) {
    result[i % num_channels].push_back(audio[i]);
  }

  return result;
}

} // namespace giga_edge
