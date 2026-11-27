#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class IMicrophone {
public:
  using OnChunk = std::function<void(std::vector<float> samples, int32_t chunk_id)>;
  using OnFinished = std::function<void()>;
  virtual ~IMicrophone() = default;
  virtual void Stop() = 0;
};

std::shared_ptr<IMicrophone> CreateMacosMicrophone(
  int sample_rate_hz, int chunk_duration_ms, IMicrophone::OnChunk on_chunk
);

std::shared_ptr<IMicrophone> CreateArecordMicrophone(
  int sample_rate_hz,
  int chunk_duration_ms,
  std::string capture_device,
  IMicrophone::OnChunk on_chunk,
  IMicrophone::OnFinished on_finished = {}
);

} // namespace giga_edge
