#pragma once

#include "giga_edge/modules/model/interpreter/tensor.h"

#include <cstddef>
#include <string>
#include <vector>

namespace giga_edge {

// Contiguous float32/int32; the interpreter converts from CoreML's fp16/strided arrays.
class CoreMLTensor : public Tensor {
public:
  CoreMLTensor(std::string name, int index, DataType type, std::vector<int> shape);

  int DimsNum() const override;
  int Dim(int index) const override;
  std::string_view GetName() const override;
  int GetIndex() const override;
  DataType GetDataType() const override;
  bool IsSameType(const Tensor& other) const override;

  float* FloatData() override;
  int* IntData() override;
  int64_t* Int64Data() override;
  char* Data() override;
  int Size() const override;

  void Reshape(std::vector<int> shape);

private:
  std::string name_;
  int index_;
  DataType type_;
  std::vector<int> shape_;
  std::vector<std::byte> data_;
};

} // namespace giga_edge
