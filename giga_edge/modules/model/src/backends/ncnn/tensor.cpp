#include "giga_edge/modules/model/backends/ncnn/tensor.h"

#include <cassert>
#include <stdexcept>

namespace giga_edge {

int NcnnTensor::DimsNum() const {
  return shape_.size();
}

int NcnnTensor::Dim(int index) const {
  assert(index < DimsNum());
  return shape_[index];
}

std::string_view NcnnTensor::GetName() const {
  return name_;
}

int NcnnTensor::GetIndex() const {
  return index_;
}

Tensor::DataType NcnnTensor::GetDataType() const {
  return DataType::FLOAT32;
}

bool NcnnTensor::IsSameType(const Tensor& other) const {
  return true;
}

float* NcnnTensor::FloatData() {
  return static_cast<float*>(impl_->data);
}

int* NcnnTensor::IntData() {
  throw std::runtime_error("Not implemented NCNN IntData");
}

int64_t* NcnnTensor::Int64Data() {
  throw std::runtime_error("Not implemented NCNN Int64Data");
}

char* NcnnTensor::Data() {
  return static_cast<char*>(impl_->data);
}

int NcnnTensor::Size() const {
  int size = 1;
  for (auto d : shape_) {
    size *= d;
  }
  assert(GetDataType() == DataType::FLOAT32);
  return size * sizeof(float);
}

NcnnTensor::NcnnTensor(const ncnn::Mat* impl, const std::string& name, int index)
  : impl_(impl)
  , name_(name)
  , index_(index) {
  shape_ = GetShape(*impl);
}

ncnn::Mat NcnnTensor::GetShapeMat(const std::vector<int>& shape) {
  // (shape[0]) -> (value) -> (w).
  const int dims = shape.size();
  if (dims == 1) {
    int w = shape[0];
    return ncnn::Mat(w);
    // (shape[0], shape[1]) -> (batch, value) -> (h, w).
  } else if (dims == 2) {
    int h = shape[0];
    int w = shape[1];
    return ncnn::Mat(w, h);
    // (shape[0], shape[1], shape[2]) -> (batch, ctx, mel) -> (c, h, w).
  } else if (dims == 3) {
    int c = shape[0];
    int h = shape[1];
    int w = shape[2];
    if (c > 1) {
      // A multi-channel Mat splits its data by an aligned cstep: a flat read of
      // FloatData()/Data() by Size() yields garbage between the channels.
      throw std::runtime_error(
        "NCNN 3D tensors with more than one channel are not supported: the aligned channel stride breaks flat data "
        "access"
      );
    }
    return ncnn::Mat(w, h, c);
  }
  throw std::runtime_error("NCNN 4D tensors are not supported.");
}

std::vector<int> NcnnTensor::GetShape(const ncnn::Mat& m) {
  if (m.dims == 1) {
    return std::vector<int>{m.w};
  } else if (m.dims == 2) {
    return std::vector<int>{m.h, m.w};
  } else if (m.dims == 3) {
    if (m.c > 1 && m.cstep != static_cast<size_t>(m.w) * m.h) {
      // The channels are spaced by an aligned cstep: a flat read of FloatData()/Data() by Size()
      // would land in the gap between the channels.
      throw std::runtime_error(
        "NCNN 3D tensors with a strided multi-channel layout are not supported: flat data access would cross the "
        "channel gap"
      );
    }
    return std::vector<int>{m.c, m.h, m.w};
  }
  throw std::runtime_error("NCNN 4D tensors are not supported.");
}

ncnn::Mat NcnnTensor::Zeros(const std::vector<int>& shape) {
  auto shape_mat = NcnnTensor::GetShapeMat(shape);
  ncnn::Mat m(shape_mat);
  m.fill(0.f);
  return m;
}

NcnnTensor::~NcnnTensor() {}

} // namespace giga_edge