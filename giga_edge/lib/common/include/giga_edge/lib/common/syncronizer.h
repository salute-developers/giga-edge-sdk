#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <tuple>
#include <utility>

namespace giga_edge {

template<typename... Ts>
class Syncronizer {
  template<class T>
  struct QueuedValue {
    std::optional<uint32_t> seq_id;
    T value;
  };

public:
  using Callback = std::function<void(Ts...)>;

  explicit Syncronizer(Callback callback)
    : callback_(std::move(callback)) {}

  void Start(uint32_t seq_id) {
    std::lock_guard lock(mutex_);
    start_seq_id_ = seq_id;
    DropSkipped<0>();
    TryCall();
  }

  template<size_t Index>
  void Push(typename std::tuple_element<Index, std::tuple<Ts...>>::type value) {
    std::lock_guard lock(mutex_);
    std::get<Index>(queues_).push(
      QueuedValue<typename std::tuple_element<Index, std::tuple<Ts...>>::type>{
        .value = std::move(value),
      }
    );
    TryCall();
  }

  template<size_t Index>
  void Push(uint32_t seq_id, typename std::tuple_element<Index, std::tuple<Ts...>>::type value) {
    std::lock_guard lock(mutex_);
    if (start_seq_id_.has_value() && seq_id < start_seq_id_.value()) {
      return;
    }
    std::get<Index>(queues_).push(
      QueuedValue<typename std::tuple_element<Index, std::tuple<Ts...>>::type>{
        .seq_id = seq_id,
        .value = std::move(value),
      }
    );
    TryCall();
  }

private:
  void TryCall() {
    if (AllIsReady<0>()) {
      Call<0>();
    }
  }

  template<size_t I>
  void DropSkipped() {
    if constexpr (I != sizeof...(Ts)) {
      if (start_seq_id_.has_value()) {
        auto& q = std::get<I>(queues_);
        while (!q.empty() && q.front().seq_id.has_value() && q.front().seq_id.value() < start_seq_id_.value()) {
          q.pop();
        }
      }
      DropSkipped<I + 1>();
    }
  }

  template<size_t I>
  bool AllIsReady() {
    if constexpr (I == sizeof...(Ts)) {
      return true;
    } else {
      return !std::get<I>(queues_).empty() && AllIsReady<I + 1>();
    }
  }

  template<size_t I, typename... Args>
  void Call(Args&&... args) {
    if constexpr (I == sizeof...(Ts)) {
      callback_(std::forward<Args>(args)...);
    } else {
      auto& q = std::get<I>(queues_);
      auto val = std::move(q.front().value);
      q.pop();
      Call<I + 1>(std::forward<Args>(args)..., std::move(val));
    }
  }

  std::mutex mutex_;
  std::optional<uint32_t> start_seq_id_;
  std::tuple<std::queue<QueuedValue<Ts>>...> queues_;
  Callback callback_;
};

} // namespace giga_edge
