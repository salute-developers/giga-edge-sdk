#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <string_view>
#include <vector>

namespace giga_edge {

template<class T>
std::vector<T> ParseTokenDictionary(
  std::string_view data, char delimeter, const std::function<T(std::string_view)>& token_parser
) {
  std::vector<T> result;

  for (auto pos = data.find(delimeter); pos != std::string_view::npos; pos = data.find(delimeter)) {
    result.push_back(token_parser(data.substr(0, pos)));
    data = data.substr(pos + 1);
  }
  if (!data.empty()) {
    result.push_back(token_parser(data));
  }
  return result;
}

template<class T>
std::vector<T> TokenDictionaryFromFile(
  const IDataReaderPtr& data_reader,
  const std::string& dict_path,
  const std::function<T(std::string_view)>& token_parser
) {
  auto dict_data = data_reader->ReadTransientData(dict_path);
  auto [data, size] = EnsureNotNull(dict_data, dict_path)->Get();
  return ParseTokenDictionary(std::string_view{data, size}, '\n', token_parser);
}

template<class T>
T GetToken(const std::vector<float>& probs, const std::vector<T>& dict, int idx = 0) {
  auto it = probs.begin() + idx * dict.size();
  return dict.at(std::max_element(it, it + dict.size()) - it);
}

} // namespace giga_edge
