#pragma once

#include "giga_edge/modules/model/interpreter/tensor.h"

#include "onnxruntime/onnxruntime_cxx_api.h"

#include <string>

namespace giga_edge {

class OnnxruntimeTensor : public Tensor {
public:
  OnnxruntimeTensor(Ort::Value* value, const std::string& name, int index);
  ~OnnxruntimeTensor() override = default;

  [[nodiscard]] int DimsNum() const override;
  [[nodiscard]] int Dim(int index) const override;
  [[nodiscard]] std::string_view GetName() const override;
  [[nodiscard]] int GetIndex() const override;
  [[nodiscard]] DataType GetDataType() const override;
  [[nodiscard]] bool IsSameType(const Tensor& other) const override;

  float* FloatData() override;
  int* IntData() override;
  int64_t* Int64Data() override;
  char* Data() override;
  [[nodiscard]] int Size() const override;

private:
  void CheckState() const;

  Ort::Value* value_;
  std::string name_;
  int index_;
};

} // namespace giga_edge
