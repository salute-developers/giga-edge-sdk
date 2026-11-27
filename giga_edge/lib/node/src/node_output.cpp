#include "giga_edge/lib/node/node_output.h"

#include "giga_edge/lib/common/scope_guard.h"
#include "giga_edge/lib/common/str_utils.h"

#include <algorithm>
#include <sstream>

namespace giga_edge {

OutputValueBase::OutputValueBase(ValueInfo value_info)
  : value_info_(std::move(value_info)) {}

const ValueInfo& OutputValueBase::value_info() const {
  return value_info_;
}

ValueInfo& OutputValueBase::value_info() {
  return value_info_;
}

std::string ValueInfo::ToString() const {
  std::ostringstream os;
  os << "ValueInfo( batch_ids = [";
  JoinStringsTo(os, batch_infos, ",", [](const ValueInfo::BatchInfo& info) {
    std::ostringstream info_os;
    info_os << "( id: " << info.id << ", is_last: " << info.is_last << ")";
    return info_os.str();
  });
  os << "], seq_id = " << seq_id << ", is_recursive = " << is_recursive << ")";
  return os.str();
}

bool ValueInfo::IsLastSeqInfo() const {
  for (const auto& batch_info : batch_infos) {
    if (!batch_info.is_last) {
      return false;
    }
  }
  return true;
}

NodeOutput::NodeOutput(const NodeOutputId& id)
  : id_(id)
  , global_offset_(0) {}

const NodeOutputId& NodeOutput::Id() {
  return id_;
}

void NodeOutput::Subscribe(const NodeId& subscriber_id, std::function<void()> notify_callback) {
  std::lock_guard lock(mutex_);
  subscriber_offsets_[subscriber_id] = global_offset_ + data_.size();
  subscriber_callbacks_changes_.emplace_back(subscriber_id, std::move(notify_callback));
}

void NodeOutput::Unsubscribe(const NodeId& subscriber_id) {
  {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] {
      return notifying_count_ == 0;
    });
    subscriber_offsets_.erase(subscriber_id);
    subscriber_callbacks_changes_.emplace_back(subscriber_id, nullptr);
    ApplyCallbacksChanges();

    ClearData();
  }
  cv_.notify_all();
}

uint32_t NodeOutput::Push(OutputValueBasePtr new_value) {
  uint32_t target;
  {
    std::lock_guard guard(mutex_);
    data_.push_back(new_value);
    ApplyCallbacksChanges();
    ClearData();
    target = global_offset_ + static_cast<uint32_t>(data_.size());
    ++notifying_count_;
  }

  ScopeGuard finish_notify([this] {
    {
      std::lock_guard guard(mutex_);
      --notifying_count_;
    }
    cv_.notify_all();
  });

  Notify();
  return target;
}

uint32_t NodeOutput::GetNextRealOffset(const NodeId& id) {
  return subscriber_offsets_.at(id) - global_offset_; // overflow is ok
}

const ValueInfo& NodeOutput::PeekValueInfo(const NodeId& id) {
  std::lock_guard guard(mutex_);
  return data_.at(GetNextRealOffset(id))->value_info();
}

bool NodeOutput::Empty() const {
  std::lock_guard guard(mutex_);
  return data_.empty() || (data_.size() == 1 && data_[0]->value_info().is_recursive);
}

bool NodeOutput::HasNext(const NodeId& id, bool batch) {
  std::lock_guard guard(mutex_);
  return HasNextImpl(GetNextRealOffset(id), batch);
}

bool NodeOutput::HasNextImpl(uint32_t real_offset, bool batch) {
  if (real_offset >= data_.size()) {
    return false;
  }

  if (batch) {
    return data_.back()->value_info().batch_infos.back().is_last
      || data_.back()->value_info().batch_infos.back().id != data_[real_offset]->value_info().batch_infos.back().id;
  }

  return true;
}

void NodeOutput::ApplyCallbacksChanges() {
  for (auto&& [subscriber, callback] : subscriber_callbacks_changes_) {
    if (callback == nullptr) {
      subscriber_callbacks_.erase(subscriber);
    } else {
      subscriber_callbacks_.emplace(subscriber, std::move(callback));
    }
  }
  subscriber_callbacks_changes_.clear();
}

void NodeOutput::Notify() {
  for (const auto& [_, callback] : subscriber_callbacks_) {
    callback();
  }
}

size_t NodeOutput::Size() const {
  std::lock_guard guard(mutex_);
  return std::count_if(data_.begin(), data_.end(), [](const OutputValueBasePtr value) {
    // If value is recursive, then it has no batch infos.
    return value->value_info().is_recursive || value->value_info().batch_infos.back().is_last;
  });
}

OutputValueBasePtr NodeOutput::ConsumeSingle(const NodeId& id) {
  std::lock_guard guard(mutex_);
  return ConsumeOne(id);
}

std::vector<OutputValueBasePtr> NodeOutput::ConsumeBatch(const NodeId& id) {
  std::lock_guard guard(mutex_);
  std::vector<OutputValueBasePtr> result;

  while (true) {
    auto item = ConsumeOne(id);

    result.push_back(item);
    if (item->value_info().batch_infos.back().is_last) {
      break;
    }
  }

  return result;
}

OutputValueBasePtr NodeOutput::ConsumeOne(const NodeId& id) {
  auto result = data_.at(subscriber_offsets_.at(id) - global_offset_);
  ++subscriber_offsets_.at(id);

  ClearData();
  cv_.notify_all();
  return result;
}

void NodeOutput::WaitConsumedTo(uint32_t target_offset) {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this, target_offset]() {
    for (const auto& [_, offset] : subscriber_offsets_) {
      if (static_cast<int32_t>(offset - target_offset) < 0) {
        return false;
      }
    }
    return true;
  });
}

void NodeOutput::ClearData() {
  if (subscriber_offsets_.empty()) {
    global_offset_ += data_.size();
    data_.clear();
  } else {
    auto off_it = subscriber_offsets_.cbegin();
    auto new_real_offset = off_it->second - global_offset_;
    for (const auto end_it = subscriber_offsets_.cend(); ++off_it != end_it;) {
      new_real_offset = std::min(new_real_offset, off_it->second - global_offset_);
    }
    data_.erase(data_.begin(), data_.begin() + new_real_offset);
    global_offset_ += new_real_offset;
  }
}

} // namespace giga_edge
