#pragma once

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/node/output_value.h"
#include "giga_edge/lib/node/types.h"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace giga_edge {

class OutputsData : public NonCopyable {
public:
  OutputsData(
    const NodeId& node_id,
    const ValueInfo& value_info,
    uint32_t output_batch_id,
    const std::vector<std::string_view>& outputs,
    std::unordered_set<std::string_view> batch_outputs,
    std::unordered_set<std::string_view> recursive_outputs
  );

  template<class T>
  void Add(std::string_view id, T value) {
    auto it = closed_outputs_.find(id);
    if (it == closed_outputs_.end()) {
      throw std::runtime_error("Node " + node_id_ + " does not have output " + std::string{id});
    }
    if (it->second) {
      throw std::runtime_error("Node " + node_id_ + " does output " + std::string{id} + " is already closed");
    }
    data_[it->first].push_back(std::make_shared<OutputValue<T>>(std::move(value), GenerateValueInfo(id)));
  }

  bool Has(std::string_view id) const {
    return closed_outputs_.find(id) != closed_outputs_.end();
  }

  void CloseOutput(std::string_view id);
  bool IsClosedOutput(std::string_view id);
  void Close();

  std::unordered_map<std::string_view, std::vector<OutputValueBasePtr>> ExtractData();

  ~OutputsData();

private:
  ValueInfo GenerateValueInfo(std::string_view output) const;

private:
  const NodeId& node_id_;
  ValueInfo value_info_;
  uint32_t output_batch_id_;

  std::unordered_set<std::string_view> batch_outputs_;
  std::unordered_set<std::string_view> recursive_outputs_;

  std::unordered_map<std::string_view, std::vector<OutputValueBasePtr>> data_;
  std::unordered_map<std::string_view, bool> closed_outputs_;
};

} // namespace giga_edge