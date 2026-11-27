#pragma once

#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/model/models/crnn/params.h"

#include <functional>

namespace giga_edge {

class IBaseCRNNModel {
public:
  virtual void Reset() = 0;

  virtual std::vector<float> PredictSingleStep(const float* input, size_t num_elements) = 0;

  virtual size_t GetInputSize() = 0;

  virtual size_t GetOutputSize() = 0;

  virtual ~IBaseCRNNModel() = default;
};

class BaseCRNNModelImpl : public IBaseCRNNModel {
public:
  // If no data_reader is provided, path is the path on the file system.
  BaseCRNNModelImpl(
    const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr
  );

  ~BaseCRNNModelImpl() override = default;

  size_t GetInputSize() override;

  size_t GetOutputSize() override;

protected:
  virtual void CommonInit(int num_mels) = 0;

  InterpreterPtr interpreter_;
  size_t input_tensor_size_;
  size_t output_tensor_size_;
};

} // namespace giga_edge
