#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/model/backends/onnxruntime/onnx_environment.h"
#include "giga_edge/modules/model/backends/onnxruntime/params.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include "onnxruntime/onnxruntime_cxx_api.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class OnnxruntimeInterpreter : public Interpreter {
public:
  explicit OnnxruntimeInterpreter(
    const OnnxInterpreterParams& params,
    const std::string& path,
    const IDataReaderPtr& data_reader,
    const std::map<std::string, EnvironmentPtr>* environments
  );

  ~OnnxruntimeInterpreter() override = default;

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
  void Init();
  int GetInputId(const std::string& name) const;
  int GetOutputId(const std::string& name) const;

  OnnxInterpreterParams params_;
  // The graph releases environments before its nodes. Keep a shared environment alive for this session.
  std::shared_ptr<OnnxEnvironment> shared_env_;
  Ort::Env env_{nullptr};
  Ort::SessionOptions session_options_;
  Ort::RunOptions run_options_;
  Ort::Session session_;
  Ort::AllocatorWithDefaultOptions allocator_;

  IDataSpanPtr model_data_span_;

  std::vector<std::string> input_names_;
  std::vector<std::string> output_names_;

  // C-style string pointers for the onnxruntime API (keeps Invoke fast).
  std::vector<const char*> input_name_ptrs_;
  std::vector<const char*> output_name_ptrs_;

  std::unordered_map<std::string, int> input_name_to_index_;
  std::unordered_map<std::string, int> output_name_to_index_;

  std::vector<std::vector<char>> input_buffers_;
  std::vector<Ort::Value> input_tensors_;
  std::vector<Ort::Value> output_tensors_;
};

} // namespace giga_edge
