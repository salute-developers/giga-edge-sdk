#include "giga_edge/modules/model/backends/onnxruntime/tensor.h"

#include "giga_edge/modules/model/backends/onnxruntime/utils.h"

#include <iostream>
#include <stdexcept>

namespace giga_edge {

OnnxruntimeTensor::OnnxruntimeTensor(Ort::Value* value, const std::string& name, int index)
  : value_(value)
  , name_(name)
  , index_(index) {
  if (!value_ || !value_->IsTensor()) {
    throw std::invalid_argument(
      "OnnxruntimeTensor created with invalid or non-tensor Ort::Value for tensor '" + name_ + "'"
    );
  }
}

void OnnxruntimeTensor::CheckState() const {
  if (!value_ || !value_->IsTensor()) {
    throw std::runtime_error("Tensor '" + name_ + "' is in an invalid state.");
  }
}

int OnnxruntimeTensor::DimsNum() const {
  CheckState();
  auto info = value_->GetTensorTypeAndShapeInfo();
  return info.GetDimensionsCount();
}

int OnnxruntimeTensor::Dim(int index) const {
  CheckState();
  auto info = value_->GetTensorTypeAndShapeInfo();
  const auto& shape = info.GetShape();
  if (index < 0 || static_cast<size_t>(index) >= shape.size()) {
    throw std::out_of_range("Dimension index out of bounds for tensor " + name_);
  }
  return info.GetShape()[index];
}

std::string_view OnnxruntimeTensor::GetName() const {
  return name_;
}

int OnnxruntimeTensor::GetIndex() const {
  return index_;
}

Tensor::DataType OnnxruntimeTensor::GetDataType() const {
  CheckState();
  auto info = value_->GetTensorTypeAndShapeInfo();
  return onnx_utils::ToTensorDataType(info.GetElementType());
}

float* OnnxruntimeTensor::FloatData() {
  CheckState();
  return value_->GetTensorMutableData<float>();
}

int* OnnxruntimeTensor::IntData() {
  CheckState();
  return value_->GetTensorMutableData<int>();
}

int64_t* OnnxruntimeTensor::Int64Data() {
  CheckState();
  return value_->GetTensorMutableData<int64_t>();
}

char* OnnxruntimeTensor::Data() {
  CheckState();
  return static_cast<char*>(value_->GetTensorMutableData<void>());
}

int OnnxruntimeTensor::Size() const {
  CheckState();
  auto info = value_->GetTensorTypeAndShapeInfo();
  return info.GetElementCount() * onnx_utils::GetElementSize(info.GetElementType());
}

bool OnnxruntimeTensor::IsSameType(const Tensor& other) const {
  CheckState();
  const auto& other_onnx = dynamic_cast<const OnnxruntimeTensor&>(other);
  other_onnx.CheckState();

  auto this_info = value_->GetTensorTypeAndShapeInfo();
  auto other_info = other_onnx.value_->GetTensorTypeAndShapeInfo();
  return this_info.GetElementType() == other_info.GetElementType();
}

} // namespace giga_edge
