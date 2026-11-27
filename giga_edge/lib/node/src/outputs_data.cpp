#include "giga_edge/lib/node/outputs_data.h"

#include <iostream>

namespace giga_edge {

OutputsData::OutputsData(
  const NodeId& node_id,
  const ValueInfo& value_info,
  uint32_t output_batch_id,
  const std::vector<std::string_view>& outputs,
  std::unordered_set<std::string_view> batch_outputs,
  std::unordered_set<std::string_view> recursive_outputs
)
  : node_id_(node_id)
  , value_info_(value_info)
  , output_batch_id_(output_batch_id)
  , batch_outputs_(std::move(batch_outputs))
  , recursive_outputs_(std::move(recursive_outputs)) {
  for (const auto& output : outputs) {
    closed_outputs_[output] = false;
  }
}

ValueInfo OutputsData::GenerateValueInfo(std::string_view output) const {
  if (recursive_outputs_.find(output) != recursive_outputs_.end()) {
    return ValueInfo{.is_recursive = true};
  }

  ValueInfo result = value_info_;
  if (batch_outputs_.find(output) != batch_outputs_.end()) {
    result.batch_infos.push_back({.id = output_batch_id_});
  }
  return result;
}

void OutputsData::CloseOutput(std::string_view id) {
  closed_outputs_.at(id) = true;
}

bool OutputsData::IsClosedOutput(std::string_view id) {
  return closed_outputs_.at(id);
}

void OutputsData::Close() {
  for (const auto& [id, value] : data_) {
    CloseOutput(id);
  }

  for (const auto& [id, value] : closed_outputs_) {
    if (!closed_outputs_.at(id)) {
      throw std::runtime_error(std::string{node_id_} + ": output '" + std::string{id} + "' is not closed");
    }
  }
}

std::unordered_map<std::string_view, std::vector<OutputValueBasePtr>> OutputsData::ExtractData() {
  for (const auto& [id, value] : data_) {
    bool is_batch = (batch_outputs_.find(id) != batch_outputs_.end());
    if (!is_batch) {
      if (value.size() > 1) {
        throw std::runtime_error(
          std::string{node_id_} + ": output '" + std::string{id} + "' is single value and already has data"
        );
      }
      CloseOutput(id);
    }

    if (is_batch && IsClosedOutput(id)) {
      value.back()->value_info().batch_infos.back().is_last = true;
    }
  }

  return {std::move(data_)};
}

OutputsData::~OutputsData() {
  if (!data_.empty()) {
    std::cerr << "ERROR: " << node_id_ << " has not pushed outputs" << std::endl;
  }
}

} // namespace giga_edge