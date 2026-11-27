#include "giga_edge/lib/node/inputs_data.h"

namespace giga_edge {

InputsData::InputsData(
  const NodeId& node_id,
  std::unordered_map<std::string, OutputValueBasePtr> single_data,
  std::unordered_map<std::string, std::vector<OutputValueBasePtr>> batch_data
)
  : node_id_(node_id)
  , single_data_(std::move(single_data))
  , batch_data_(std::move(batch_data)) {
  InitValueInfo();
}

bool InputsData::Contains(const std::string& id) const {
  return single_data_.find(id) != single_data_.end() || batch_data_.find(id) != batch_data_.end();
}

const ValueInfo& InputsData::GetValueInfo() const {
  return value_info_.value();
}

bool InputsData::CompareValueInfo(const ValueInfo& value_info, bool is_batch) {
  if (value_info_->seq_id != value_info.seq_id) {
    return false;
  }

  if (value_info_->batch_infos.size() + is_batch != value_info.batch_infos.size()) {
    return false;
  }

  for (int i = 0; i < value_info_->batch_infos.size(); ++i) {
    if (value_info_->batch_infos[i].id != value_info.batch_infos[i].id) {
      return false;
    }
  }

  return true;
}

void InputsData::ProcessValueInfo(const OutputValueBasePtr& value, bool is_batch) {
  if (value->value_info().is_recursive) {
    return;
  }

  if (!value_info_.has_value()) {
    value_info_ = value->value_info();
    if (is_batch) {
      value_info_->batch_infos.pop_back();
    }
  } else {
    if (!CompareValueInfo(value->value_info(), is_batch)) {
      throw std::runtime_error(
        node_id_ + ": value_info conflict inputs. " + value_info_->ToString() + " != " + value->value_info().ToString()
      );
    }
  }
}

void InputsData::InitValueInfo() {
  for (const auto& [id, value] : single_data_) {
    ProcessValueInfo(value, false);
  }

  for (const auto& [id, batch] : batch_data_) {
    for (const auto& value : batch) {
      ProcessValueInfo(value, true);
    }
  }
}

} // namespace giga_edge