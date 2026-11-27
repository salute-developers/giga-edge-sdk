#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace giga_edge {

struct ChunkIteratorParams {
  int32_t chunk_size;
  int32_t step_size;
};

template<class T>
class ChunkIterator {
public:
  explicit ChunkIterator(const ChunkIteratorParams& params);

  void Push(const std::vector<T>& items);

  bool HasNext() const;

  std::vector<T> GetAll();

  std::vector<T> Next();

  void Reset();

private:
  const ChunkIteratorParams params_;
  std::deque<T> state_;
};

} // namespace giga_edge
