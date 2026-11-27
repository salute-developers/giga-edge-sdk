#include "giga_edge/modules/model/interpreter/tensor.h"

#include "giga_edge/lib/enum/enum.h"

#include <stdexcept>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(Tensor::DataType, "float", "int32", "int64")

namespace giga_edge {

int Tensor::GetIndex() const {
  throw std::runtime_error("Tensor::GetIndex is not implemented");
}

Tensor::DataType Tensor::GetDataType() const {
  throw std::runtime_error("Tensor::GetDataType is not implemented");
}

bool Tensor::IsSameType([[maybe_unused]] const Tensor& other) const {
  throw std::runtime_error("Tensor::IsSameType is not implemented");
}

int Tensor::NumElements() const {
  int total_elements = 1;
  for (int count = DimsNum(), i = 0; i < count; ++i) {
    total_elements *= Dim(i);
  }
  return total_elements;
}

void Tensor::GetShape(std::vector<int>& shape) const {
  shape.resize(DimsNum());
  for (unsigned i = 0; i < shape.size(); ++i) {
    shape[i] = Dim(i);
  }
}

std::vector<int> Tensor::GetShape() const {
  std::vector<int> shape;
  GetShape(shape);
  return shape;
}

} // namespace giga_edge
