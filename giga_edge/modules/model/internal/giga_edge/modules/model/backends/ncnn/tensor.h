#pragma once

#include "giga_edge/modules/model/interpreter/tensor.h"

#include "ncnn/mat.h"

#include <string_view>

namespace giga_edge {
class NcnnTensor : public Tensor {
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
  NcnnTensor(const ncnn::Mat* impl, const std::string& name, int index);
  ~NcnnTensor();
  static ncnn::Mat GetShapeMat(const std::vector<int>& shape);
  static std::vector<int> GetShape(const ncnn::Mat& m);
  static ncnn::Mat Zeros(const std::vector<int>& shape);
private:
  const ncnn::Mat* impl_;
  std::string name_;
  int index_;
  std::vector<int> shape_;
};
} // namespace giga_edge