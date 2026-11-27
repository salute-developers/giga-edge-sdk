#include "giga_edge/modules/model/backends/coreml/tensor.h"

#include <functional>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace giga_edge {

namespace {

size_t ElementSize(Tensor::DataType type) {
  switch (type) {
    case Tensor::DataType::FLOAT32:
      return sizeof(float);
    case Tensor::DataType::INT32:
      return sizeof(int32_t);
    case Tensor::DataType::INT64:
      return sizeof(int64_t);
  }
  throw std::logic_error("CoreMLTensor: unknown data type");
}

size_t Elements(const std::vector<int>& shape) {
  return std::accumulate(shape.begin(), shape.end(), size_t{1}, [](size_t total, int dim) {
    return total * static_cast<size_t>(dim);
  });
}

} // namespace

CoreMLTensor::CoreMLTensor(std::string name, int index, DataType type, std::vector<int> shape)
  : name_(std::move(name))
  , index_(index)
  , type_(type) {
  Reshape(std::move(shape));
}

void CoreMLTensor::Reshape(std::vector<int> shape) {
  shape_ = std::move(shape);
  data_.resize(Elements(shape_) * ElementSize(type_));
}

int CoreMLTensor::DimsNum() const {
  return static_cast<int>(shape_.size());
}

int CoreMLTensor::Dim(int index) const {
  return shape_.at(index);
}

std::string_view CoreMLTensor::GetName() const {
  return name_;
}

int CoreMLTensor::GetIndex() const {
  return index_;
}

Tensor::DataType CoreMLTensor::GetDataType() const {
  return type_;
}

bool CoreMLTensor::IsSameType(const Tensor& other) const {
  return type_ == other.GetDataType();
}

float* CoreMLTensor::FloatData() {
  if (type_ != DataType::FLOAT32) {
    throw std::runtime_error("CoreML tensor '" + name_ + "' is not float");
  }
  return reinterpret_cast<float*>(data_.data());
}

int* CoreMLTensor::IntData() {
  if (type_ != DataType::INT32) {
    throw std::runtime_error("CoreML tensor '" + name_ + "' is not int32");
  }
  return reinterpret_cast<int*>(data_.data());
}

int64_t* CoreMLTensor::Int64Data() {
  throw std::runtime_error("CoreML tensor '" + name_ + "': CoreML has no int64 multi-arrays");
}

char* CoreMLTensor::Data() {
  return reinterpret_cast<char*>(data_.data());
}

int CoreMLTensor::Size() const {
  return static_cast<int>(data_.size());
}

} // namespace giga_edge
