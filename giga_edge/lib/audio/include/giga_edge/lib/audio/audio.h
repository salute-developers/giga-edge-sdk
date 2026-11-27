#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace giga_edge {

using ChanneledAudio = std::vector<std::vector<float>>;

std::vector<float> LoadWav(const std::string& path);

std::vector<float> LoadWav(std::vector<uint8_t>& data);

std::vector<float> LoadWavFromContent(const std::string& content);

std::vector<std::vector<float>> ChunkWav(const std::vector<float>& wav, int chunk_size);
std::vector<std::vector<float>> ChunkWav(const std::vector<float>& wav, int sample_rate, int chunk_size_ms);

std::vector<ChanneledAudio> ChunkWav(const ChanneledAudio& audio, int chunk_size);
std::vector<ChanneledAudio> ChunkWav(const ChanneledAudio& wav, int sample_rate, int chunk_size_ms);

std::vector<float> InterleaveAudio(const ChanneledAudio& audio, const std::vector<int>& channel_indices = {});
ChanneledAudio DeinterleaveAudio(const std::vector<float>& audio, int num_channels);

} // namespace giga_edge
