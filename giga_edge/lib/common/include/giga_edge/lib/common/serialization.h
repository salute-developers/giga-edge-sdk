#pragma once

#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

std::optional<std::vector<char>> SerializeRawAudioBinary(const std::vector<std::vector<float>>& raw_audio);
std::vector<std::vector<float>> DeserializeRawAudioBinary(const std::vector<char>& payload);
std::string ComputeHash64(const std::vector<char>& bytes);

} // namespace giga_edge