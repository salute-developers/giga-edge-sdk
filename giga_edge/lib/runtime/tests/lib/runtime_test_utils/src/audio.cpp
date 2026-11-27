#include "giga_edge/lib/runtime/tests/runtime_test_utils/audio.h"

#include "giga_edge/lib/audio/audio.h"

#include <cassert>

namespace giga_edge::tests {

namespace {

void ApplyAudioRandom(Runtime* runtime, const ChunkedAudioData& input_chunks) {
  std::unordered_map<std::string, int32_t> total_samples_passed;
  std::unordered_map<std::string, std::deque<std::vector<float>>> input_data;

  for (const auto& [input_name, data] : input_chunks) {
    total_samples_passed[input_name] = 0;
    input_data[input_name] = {data.begin(), data.end()};
  }

  while (!input_data.empty()) {
    auto it = std::next(
      input_data.begin(),
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.rand): test-only noise source
      rand() % input_data.size()
    );

    total_samples_passed[it->first] += it->second.front().size();
    runtime->Push({}, it->first, it->second.front(), total_samples_passed[it->first]);

    it->second.pop_front();
    if (it->second.empty()) {
      input_data.erase(it);
    }
  }
}

void ApplyAudioAlternatingAll(Runtime* runtime, const ChunkedAudioData& input_chunks) {
  for (const auto& [input_name, chunks] : input_chunks) {
    int total_samples_passed = 0;
    for (auto& chunk : chunks) {
      total_samples_passed += chunk.size();
      runtime->Push({}, input_name, chunk, total_samples_passed);
    }
  }
}

void ApplyAudioAlternating(Runtime* runtime, const ChunkedAudioData& input_chunks) {
  int num_chunks = input_chunks.empty() ? 0 : input_chunks.begin()->second.size();
  int total_samples_passed = 0;

  for (int i = 0; i < num_chunks; ++i) {
    total_samples_passed += input_chunks.begin()->second.at(0).size();
    for (const auto& [input_name, wav_data] : input_chunks) {
      runtime->Push({}, input_name, wav_data[i], total_samples_passed);
    }
  }
}

void ApplyAudioWithReset(Runtime* runtime, const ChunkedAudioData& input_chunks) {
  for (const auto& [input_name, chunks] : input_chunks) {
    int total_samples_passed = 0;
    for (auto& chunk : chunks) {
      total_samples_passed += chunk.size();
      runtime->Push({}, input_name, chunk, total_samples_passed);
    }
    runtime->Reset();
  }
}

} // namespace

ChunkedAudioData ChunkWavs(const AudioData& wav_data, int chunk_size) {
  int num_chunks = 0;
  std::unordered_map<std::string, std::vector<std::vector<float>>> result;

  for (const auto& [wav_name, data] : wav_data) {
    result[wav_name] = ChunkWav(data, 16000, chunk_size);

    if (num_chunks == 0) {
      num_chunks = result.at(wav_name).size();
    } else {
      assert(num_chunks == static_cast<int>(result.at(wav_name).size()));
    }
  }

  return result;
}

void ApplyAudio(Runtime* runtime, const ChunkedAudioData& audio_data, InputOrder input_order) {
  switch (input_order) {
    case InputOrder::ALTERNATING: {
      ApplyAudioAlternating(runtime, audio_data);
      break;
    }
    case InputOrder::ALTERNATING_ALL: {
      ApplyAudioAlternatingAll(runtime, audio_data);
      break;
    }
    case InputOrder::RANDOM: {
      ApplyAudioRandom(runtime, audio_data);
      break;
    }
    case InputOrder::WITH_RESET: {
      ApplyAudioWithReset(runtime, audio_data);
      break;
    }
  }
}

} // namespace giga_edge::tests