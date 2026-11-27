#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include "ncnn/net.h"

#include <unordered_map>

namespace giga_edge {
class NcnnInterpreter : public Interpreter {
public:
  // If no data_reader is provided, path is the path on the file system.
  explicit NcnnInterpreter(const std::string& path, const IDataReaderPtr& data_reader = nullptr);

  void Invoke() override;

  TensorPtr Input(const std::string& name) const override;
  TensorPtr Output(const std::string& name) const override;

  std::vector<std::string> InputNames() const override;
  std::vector<std::string> OutputNames() const override;

  TensorPtr Input(int id) const override;
  TensorPtr Output(int id) const override;

  int InputsCount() const override;
  int OutputsCount() const override;

private:
  struct TensorInfo {
    enum Type { INPUT, OUTPUT };
    std::string name;
    Type type;
    std::vector<int> shape;
  };

  struct IOConfig {
    std::vector<TensorInfo> inputs;
    std::vector<TensorInfo> outputs;
  };

  static IOConfig ParseJsonConfigPath(const std::string& path);
  static IOConfig ParseJsonConfigString(const std::string& json_content);
  void Init(const IOConfig& ioConfig);
  int GetInputId(const std::string& name) const;
  int GetOutputId(const std::string& name) const;

  IDataSpanPtr model_param_data_;
  IDataSpanPtr model_bin_data_;
  IDataSpanPtr model_json_data_;
  ncnn::Net model_;

  std::vector<std::string> input_names_;
  std::vector<std::string> output_names_;

  std::unordered_map<std::string, int> input_name_to_index_;
  std::unordered_map<std::string, int> output_name_to_index_;

  std::vector<ncnn::Mat> input_tensors_;
  std::vector<ncnn::Mat> output_tensors_;
};
} // namespace giga_edge
