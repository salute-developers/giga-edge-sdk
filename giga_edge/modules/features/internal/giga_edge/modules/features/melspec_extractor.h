#pragma once

#include <memory>
#include <vector>

class IMelSpecExtractor {
public:
  virtual std::vector<float> GetFeatures(const std::vector<float>& wav) = 0;

  virtual void Reset() {};

  virtual ~IMelSpecExtractor() = default;
};