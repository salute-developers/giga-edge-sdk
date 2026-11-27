#pragma once

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/node/output_value.h"
#include "giga_edge/lib/node/types.h"

#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class InputsData : public NonCopyable {
public:
  InputsData(
    const NodeId& node_id,
    std::unordered_map<std::string, OutputValueBasePtr> single_data,
    std::unordered_map<std::string, std::vector<OutputValueBasePtr>> batch_data
  );

  const ValueInfo& GetValueInfo() const;

  bool Contains(const std::string& id) const;

  template<class T>
  const std::shared_ptr<const T> GetSingleShared(const std::string& id) const {
    auto result = std::dynamic_pointer_cast<OutputValue<T>>(single_data_.at(id));
    if (result == nullptr) {
      throw std::runtime_error(node_id_ + ": cannot cast data for input " + id);
    }
    return {std::move(result), &result->value()};
  }

  template<class T>
  const T& GetSingle(const std::string& id) const {
    auto result = dynamic_cast<OutputValue<T>*>(single_data_.at(id).get());
    if (result == nullptr) {
      throw std::runtime_error(node_id_ + ": cannot cast data for input " + id);
    }
    return result->value();
  }

  template<class T>
  std::vector<const T*> GetBatch(const std::string& id) const {
    std::vector<const T*> result;

    const auto& data = batch_data_.at(id);
    result.reserve(data.size());

    for (const auto& item : data) {
      auto item_ptr = dynamic_cast<OutputValue<T>*>(item.get());
      if (item_ptr == nullptr) {
        throw std::runtime_error(node_id_ + ": cannot cast data for input " + id);
      }
      result.push_back(&item_ptr->value());
    }

    return result;
  }

private:
  void InitValueInfo();
  void ProcessValueInfo(const OutputValueBasePtr& value, bool is_batch);
  bool CompareValueInfo(const ValueInfo& value_info, bool is_batch);

private:
  const NodeId& node_id_;

  const std::unordered_map<std::string, OutputValueBasePtr> single_data_;
  const std::unordered_map<std::string, std::vector<OutputValueBasePtr>> batch_data_;

  std::optional<ValueInfo> value_info_;
};

} // namespace giga_edge