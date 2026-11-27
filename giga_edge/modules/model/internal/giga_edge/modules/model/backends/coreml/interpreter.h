#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/model/backends/coreml/params.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

// Compiled .mlmodelc with fixed input shapes; tensors are ordered by name.
class CoreMLInterpreter : public Interpreter {
public:
  CoreMLInterpreter(
    const CoreMLInterpreterParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr
  );
  ~CoreMLInterpreter() override;

  void Invoke() override;

  TensorPtr Input(const std::string& name) const override;
  TensorPtr Output(const std::string& name) const override;

  std::vector<std::string> InputNames() const override;
  std::vector<std::string> OutputNames() const override;

  TensorPtr Input(int id) const override;
  TensorPtr Output(int id) const override;

  int InputsCount() const override;
  int OutputsCount() const override;

  void ResizeInput(int id, const int* dims, int dims_count) override;
  void ResizeInput(const std::string& name, const int* dims, int dims_count) override;
  void ReallocateTensors() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace giga_edge
