#include "giga_edge/modules/model/backends/ncnn/interpreter.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/model/backends/ncnn/tensor.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include "ncnn/datareader.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
std::vector<std::string> ToVectorOfStrings(const std::vector<const char*>& vs) {
  std::vector<std::string> result;
  for (auto ptr : vs) {
    result.emplace_back(ptr);
  }
  return result;
}

std::string ReplaceParamExtension(const std::string& path, const std::string& extension) {
  if (path.size() < 6 || path.substr(path.size() - 6) != ".param") {
    throw std::invalid_argument("NCNN model path must end with '.param': " + path);
  }

  return path.substr(0, path.size() - 5) + extension;
}
} // namespace

namespace giga_edge {

using json = nlohmann::json;

NcnnInterpreter::IOConfig NcnnInterpreter::ParseJsonConfigPath(const std::string& path) {
  std::ifstream file(path);
  if (file.is_open()) {
    std::stringstream content;
    content << file.rdbuf();
    return ParseJsonConfigString(content.str());
  } else {
    throw std::runtime_error("Can't open ncnn model metadata json file");
  }
}

NcnnInterpreter::IOConfig NcnnInterpreter::ParseJsonConfigString(const std::string& json_content) {
  IOConfig ioConfig;
  try {
    json data = json::parse(json_content);
    for (const auto& item : data["inputs"]) {
      TensorInfo tinfo;
      tinfo.name = item["name"].get<std::string>();
      tinfo.shape = item["shape"].get<std::vector<int>>();
      tinfo.type = TensorInfo::Type::INPUT;
      ioConfig.inputs.push_back(tinfo);
    }
    for (const auto& item : data["outputs"]) {
      TensorInfo tinfo;
      tinfo.name = item["name"].get<std::string>();
      tinfo.shape = item["shape"].get<std::vector<int>>();
      tinfo.type = TensorInfo::Type::OUTPUT;
      ioConfig.outputs.push_back(tinfo);
    }
  } catch (json::parse_error&) {
    throw std::runtime_error("Ncnn model json io metadata parsing error");
  }

  return ioConfig;
}

NcnnInterpreter::NcnnInterpreter(const std::string& path, const IDataReaderPtr& data_reader) {
  int ret = 0;
  const std::string binaryFilePath = ReplaceParamExtension(path, "bin");
  const std::string jsonFilePath = ReplaceParamExtension(path, "json");

  if (!data_reader) {
    ret = model_.load_param(path.c_str());
    if (ret) {
      throw std::runtime_error("Unable to create ncnn model from file system path " + path);
    }

    ret = model_.load_model(binaryFilePath.c_str());
    if (ret) {
      throw std::runtime_error("Unable to create ncnn model from file system path " + binaryFilePath);
    }

    auto ioConfig = ParseJsonConfigPath(jsonFilePath);
    Init(ioConfig);
    return;
  }

  model_param_data_ = EnsureNotNull(data_reader->ReadPersistentData(path), path);
  const auto [param_data, param_size] = model_param_data_->Get();
  std::string param_text(param_data, param_size);
  ret = model_.load_param_mem(param_text.c_str());
  if (ret) {
    throw std::runtime_error("Unable to create ncnn model from data reader path " + path);
  }

  model_bin_data_ = EnsureNotNull(data_reader->ReadPersistentData(binaryFilePath), binaryFilePath);
  const auto [bin_data, bin_size] = model_bin_data_->Get();
  const unsigned char* model_bin_ptr = reinterpret_cast<const unsigned char*>(bin_data);
  ncnn::DataReaderFromMemory model_bin_reader(model_bin_ptr);
  ret = model_.load_model(model_bin_reader);
  if (ret) {
    throw std::runtime_error("Unable to create ncnn model from data reader path " + binaryFilePath);
  }

  model_json_data_ = EnsureNotNull(data_reader->ReadPersistentData(jsonFilePath), jsonFilePath);
  auto ioConfig = ParseJsonConfigString(ToString(model_json_data_));
  Init(ioConfig);
}

static ncnn::Mat SqueezeMat(const ncnn::Mat& m) {
  if (m.dims == 2 && m.h == 1) {
    return m.reshape(m.w);
  } else if (m.dims == 3 && m.c == 1) {
    return m.reshape(m.w, m.h);
  } else {
    return m;
  }
}

void NcnnInterpreter::Invoke() {
  auto extractor = model_.create_extractor();
  for (int i = 0; i < InputsCount(); ++i) {
    // The network takes no batch dimension: a leading dimension of 1 is dropped.
    auto squeezed_input = SqueezeMat(input_tensors_[i]);
    extractor.input(input_names_[i].c_str(), squeezed_input);
  }
  for (int i = 0; i < OutputsCount(); ++i) {
    auto& name = output_names_[i];
    auto& tensor = output_tensors_[i];
    extractor.extract(name.c_str(), tensor);
  }
}

void NcnnInterpreter::Init(const IOConfig& ioConfig) {
  // The network order (.param) defines the indices; the json config addresses tensors only by name.
  const auto bind =
    [](
      const std::vector<TensorInfo>& infos,
      const std::vector<const char*>& model_names,
      std::vector<std::string>& names,
      std::unordered_map<std::string, int>& name_to_index,
      std::vector<ncnn::Mat>& tensors
    ) {
      names = ToVectorOfStrings(model_names);
      tensors.assign(names.size(), ncnn::Mat());
      if (infos.size() != names.size()) {
        throw std::runtime_error(
          "Ncnn model io metadata count " + std::to_string(infos.size()) + " does not match the network "
          + std::to_string(names.size())
        );
      }
      for (const auto& info : infos) {
        const auto it = std::find(names.begin(), names.end(), info.name);
        if (it == names.end()) {
          throw std::runtime_error("Ncnn model io metadata names an unknown tensor: " + info.name);
        }
        const auto index = static_cast<int>(it - names.begin());
        if (!name_to_index.emplace(info.name, index).second) {
          throw std::runtime_error("Ncnn model io metadata lists tensor '" + info.name + "' more than once");
        }
        tensors[index] = NcnnTensor::Zeros(info.shape);
      }
    };
  bind(ioConfig.inputs, model_.input_names(), input_names_, input_name_to_index_, input_tensors_);
  bind(ioConfig.outputs, model_.output_names(), output_names_, output_name_to_index_, output_tensors_);
}

int NcnnInterpreter::InputsCount() const {
  return model_.input_indexes().size();
}

int NcnnInterpreter::OutputsCount() const {
  return model_.output_indexes().size();
}

std::vector<std::string> NcnnInterpreter::InputNames() const {
  return input_names_;
}

std::vector<std::string> NcnnInterpreter::OutputNames() const {
  return output_names_;
}

TensorPtr NcnnInterpreter::Input(int id) const {
  if (InputsCount() <= id) {
    return nullptr;
  }
  return std::make_shared<NcnnTensor>(&input_tensors_[id], input_names_[id], id);
}

TensorPtr NcnnInterpreter::Output(int id) const {
  if (OutputsCount() <= id) {
    return nullptr;
  }
  return std::make_shared<NcnnTensor>(&output_tensors_[id], output_names_[id], id);
}

int NcnnInterpreter::GetInputId(const std::string& name) const {
  auto search = input_name_to_index_.find(name);
  if (search != input_name_to_index_.end()) {
    return search->second;
  } else {
    throw std::runtime_error("Input tensor not found: " + name);
  }
}

int NcnnInterpreter::GetOutputId(const std::string& name) const {
  auto search = output_name_to_index_.find(name);
  if (search != output_name_to_index_.end()) {
    return search->second;
  } else {
    throw std::runtime_error("Output tensor not found: " + name);
  }
}

TensorPtr NcnnInterpreter::Input(const std::string& name) const {
  return Input(GetInputId(name));
}

TensorPtr NcnnInterpreter::Output(const std::string& name) const {
  return Output(GetOutputId(name));
}

} // namespace giga_edge