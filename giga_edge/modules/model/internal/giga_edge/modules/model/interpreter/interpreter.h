#pragma once

#include "giga_edge/lib/common/os.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/model/interpreter/params.h"
#include "giga_edge/modules/model/interpreter/tensor.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class Interpreter {
public:
  virtual ~Interpreter() = default;

  virtual void Invoke() = 0;

  virtual TensorPtr Input(const std::string& name) const = 0;
  virtual TensorPtr Output(const std::string& name) const = 0;

  virtual std::vector<std::string> InputNames() const = 0;
  virtual std::vector<std::string> OutputNames() const = 0;

  float* InputFloatData(const std::string& name) const;
  float* OutputFloatData(const std::string& name) const;

  int* InputIntData(const std::string& name) const;
  int* OutputIntData(const std::string& name) const;

  virtual TensorPtr Input(int id) const = 0;
  virtual TensorPtr Output(int id) const = 0;

  virtual int InputsCount() const = 0;
  virtual int OutputsCount() const = 0;

  // Invalidates all already existing or newly created _aliased_ Tensor objects for the provided id/name
  // until ReallocateTensors() is called. An aliased tensor is created when an input tensor acts as an output.
  virtual void ResizeInput(int id, const int* dims, int dims_count);
  virtual void ResizeInput(const std::string& name, const int* dims, int dims_count);
  // Must be called immediately after ResizeInput() call(s) before calling any data access methods
  // of Tensor or Interpreter.
  virtual void ReallocateTensors();
};

using InterpreterPtr = std::shared_ptr<Interpreter>;

// If no data_reader is provided, path is the path on the file system.
InterpreterPtr MakeInterpreter(
  const InterpreterParams& params,
  const std::string& path,
  const IDataReaderPtr& data_reader = nullptr,
  const std::map<std::string, EnvironmentPtr>* environments = nullptr
);

} // namespace giga_edge
