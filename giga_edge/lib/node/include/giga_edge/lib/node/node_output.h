#pragma once

#include "output_value.h"
#include "types.h"

#include "giga_edge/lib/common/non_copyable.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace giga_edge {

class NodeOutput : public NonCopyable {
public:
  explicit NodeOutput(const NodeOutputId& id);

  const NodeOutputId& Id();

  void Subscribe(const NodeId& subscriber_id, std::function<void()> notify_callback);
  void Unsubscribe(const NodeId& subscriber_id);

  bool HasNext(const NodeId& id, bool batch = false);

  bool Empty() const;

  size_t Size() const;

  uint32_t Push(OutputValueBasePtr new_value);

  template<class T, std::enable_if_t<!std::is_base_of_v<OutputValueBase, T>, int> = 0>
  uint32_t Push(const T& new_value, const ValueInfo& value_info) {
    return Push(std::make_shared<OutputValue<T>>(new_value, value_info));
  }

  const ValueInfo& PeekValueInfo(const NodeId& id);

  OutputValueBasePtr ConsumeSingle(const NodeId& id);

  std::vector<OutputValueBasePtr> ConsumeBatch(const NodeId& id);

  void WaitConsumedTo(uint32_t push_id);

private:
  uint32_t GetNextRealOffset(const NodeId& id);
  bool HasNextImpl(uint32_t real_offset, bool batch);

  OutputValueBasePtr ConsumeOne(const NodeId& id);

  void ClearData();
  void ApplyCallbacksChanges();

  void Notify();

private:
  NodeOutputId id_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;

  std::deque<OutputValueBasePtr> data_;

  uint32_t global_offset_;
  std::unordered_map<NodeId, uint32_t> subscriber_offsets_;
  std::unordered_map<NodeId, std::function<void()>> subscriber_callbacks_;
  std::vector<std::pair<NodeId, std::function<void()>>> subscriber_callbacks_changes_;

  uint32_t notifying_count_ = 0;
};

using NodeOutputPtr = std::shared_ptr<NodeOutput>;

} // namespace giga_edge
