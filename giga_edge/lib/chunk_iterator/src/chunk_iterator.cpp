#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"

namespace giga_edge {

template<class T>
ChunkIterator<T>::ChunkIterator(const ChunkIteratorParams& params)
  : params_(params) {}

template<class T>
void ChunkIterator<T>::Push(const std::vector<T>& items) {
  state_.insert(state_.end(), items.begin(), items.end());
}

template<class T>
bool ChunkIterator<T>::HasNext() const {
  return state_.size() >= params_.chunk_size;
}

template<class T>
std::vector<T> ChunkIterator<T>::GetAll() {
  std::vector<T> result = {state_.begin(), state_.end()};
  state_.clear();

  return result;
}

template<class T>
std::vector<T> ChunkIterator<T>::Next() {
  if (state_.size() < params_.chunk_size) {
    return {};
  }

  std::vector<T> result = {state_.begin(), state_.begin() + params_.chunk_size};
  state_.erase(state_.begin(), state_.begin() + params_.step_size);

  return result;
}

template<class T>
void ChunkIterator<T>::Reset() {
  state_.clear();
}

template class ChunkIterator<float>;
template class ChunkIterator<double>;
template class ChunkIterator<int32_t>;

} // namespace giga_edge