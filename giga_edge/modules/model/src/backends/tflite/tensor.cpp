#include "giga_edge/modules/model/backends/tflite/tensor.h"

#include <stdexcept>

namespace giga_edge {
int TfLiteTensor::DimsNum() const {
  return TfLiteTensorNumDims(impl_);
}

int TfLiteTensor::Dim(int index) const {
  if (index < TfLiteTensorNumDims(impl_)) {
    return (int)TfLiteTensorDim(impl_, index);
  }
  return 0;
}

std::string_view TfLiteTensor::GetName() const {
  return TfLiteTensorName(impl_);
}
int TfLiteTensor::GetIndex() const {
  return index_;
}

TfLiteTensor::DataType TfLiteTensor::GetDataType() const {
  const auto type = TfLiteTensorType(impl_);
  switch (type) {
    case kTfLiteFloat32:
      return DataType::FLOAT32;
    case kTfLiteInt32:
      return DataType::INT32;
    default:
      throw std::runtime_error("Unsupported tflite tensor type " + std::to_string(type));
  }
}

bool TfLiteTensor::IsSameType(const Tensor& other) const {
  return TfLiteTensorType(impl_) == TfLiteTensorType(dynamic_cast<const TfLiteTensor&>(other).impl_);
}

float* TfLiteTensor::FloatData() {
  auto tensor_type = TfLiteTensorType(impl_);
  if (tensor_type == kTfLiteFloat32) {
    auto data = TfLiteTensorData(impl_);
    return static_cast<float*>(data);
  }
  return nullptr;
}

int* TfLiteTensor::IntData() {
  auto tensor_type = TfLiteTensorType(impl_);
  if (tensor_type == kTfLiteInt32) {
    auto data = TfLiteTensorData(impl_);
    return static_cast<int*>(data);
  }
  return nullptr;
}

int64_t* TfLiteTensor::Int64Data() {
  throw std::runtime_error("Not implemented Tflite Int64Data");
}

char* TfLiteTensor::Data() {
  return static_cast<char*>(TfLiteTensorData(impl_));
}

int TfLiteTensor::Size() const {
  return TfLiteTensorByteSize(impl_);
}

TfLiteTensor::TfLiteTensor(const ::TfLiteTensor* impl, const int index)
  : impl_(impl)
  , index_(index) {}

TfLiteTensor::~TfLiteTensor() = default;
} // namespace giga_edge
