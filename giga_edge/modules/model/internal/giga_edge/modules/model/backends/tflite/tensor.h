#pragma once

#include "giga_edge/modules/model/backends/tflite/trampoline.h"
#include "giga_edge/modules/model/interpreter/tensor.h"

#include <string>

namespace giga_edge {
class TfLiteTensor : public Tensor {
public:
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
  int Size() const override;

  TfLiteTensor(const ::TfLiteTensor* impl, int index);
  ~TfLiteTensor() override;
private:
  const ::TfLiteTensor* const impl_;
  const int index_;
};
} // namespace giga_edge
