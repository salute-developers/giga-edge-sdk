#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace giga_edge {

class Tensor {
public:
  enum class DataType : unsigned char { FLOAT32, INT32, INT64 };

public:
  [[nodiscard]] virtual int DimsNum() const = 0;
  [[nodiscard]] virtual int Dim(int index) const = 0;
  [[nodiscard]] virtual std::string_view GetName() const = 0;
  // Returns the index >=0 in the corresponding input or output tensor index space of the interpreter.
  [[nodiscard]] virtual int GetIndex() const;
  [[nodiscard]] virtual DataType GetDataType() const;
  [[nodiscard]] virtual bool IsSameType(const Tensor& other) const; // compares internal interpreter tensor types
  virtual float* FloatData() = 0;
  virtual int* IntData() = 0;
  virtual int64_t* Int64Data() = 0;
  virtual char* Data() = 0;
  virtual int Size() const = 0; // size in bytes
  int NumElements() const;
  void GetShape(std::vector<int>& shape) const; // resizes given vector
  std::vector<int> GetShape() const;
  virtual ~Tensor() = default;
};

using TensorPtr = std::shared_ptr<Tensor>;

} // namespace giga_edge
