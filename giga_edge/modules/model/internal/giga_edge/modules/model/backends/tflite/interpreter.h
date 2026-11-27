#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/model/backends/tflite/params.h"
#include "giga_edge/modules/model/backends/tflite/trampoline.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include <string>
#include <vector>

namespace giga_edge {
class TfLiteInterpreter : public Interpreter {
public:
  // If no data_reader is provided, path is the path on the file system.
  TfLiteInterpreter(
    const TfLiteInterpreterParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr
  );

  void Invoke() override;

  TensorPtr Input(const std::string& name) const override;
  TensorPtr Output(const std::string& name) const override;

  std::vector<std::string> InputNames() const override;
  std::vector<std::string> OutputNames() const override;

  TensorPtr Input(int id) const override;
  TensorPtr Output(int id) const override;

  int InputsCount() const override;
  int OutputsCount() const override;

  // Invalidates all existing Tensor objects until ReallocateTensors() is called.
  void ResizeInput(int id, const int* dims, int dims_count) override;
  void ResizeInput(const std::string& name, const int* dims, int dims_count) override;
  // Must be called immediately after ResizeInput() call(s) before calling any data access methods
  // of Tensor or Interpreter.
  void ReallocateTensors() override;

private:
  TfLiteInterpreterParams params_;
  IDataSpanPtr model_data_;
  std::unique_ptr<TfLiteModel, decltype(&TfLiteModelDelete)> model_;
  // The interpreter uses the delegate until it is destroyed: declared before interpreter_ to outlive it.
  std::unique_ptr<TfLiteDelegate, void (*)(TfLiteDelegate*)> xnnpack_delegate_;
  std::unique_ptr<::TfLiteInterpreter, decltype(&TfLiteInterpreterDelete)> interpreter_;

  void PrepareInterpreter();
};
} // namespace giga_edge
