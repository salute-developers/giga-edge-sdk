#include "giga_edge/modules/model/backends/onnxruntime/interpreter.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/model/backends/onnxruntime/onnx_environment.h"
#include "giga_edge/modules/model/backends/onnxruntime/tensor.h"
#include "giga_edge/modules/model/backends/onnxruntime/utils.h"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace giga_edge {

namespace {

GraphOptimizationLevel ToOrtLevel(OnnxOptimizationLevel level) {
  switch (level) {
    case OnnxOptimizationLevel::DISABLE:
      return GraphOptimizationLevel::ORT_DISABLE_ALL;
    case OnnxOptimizationLevel::BASIC:
      return GraphOptimizationLevel::ORT_ENABLE_BASIC;
    case OnnxOptimizationLevel::EXTENDED:
      return GraphOptimizationLevel::ORT_ENABLE_EXTENDED;
    case OnnxOptimizationLevel::ALL:
      return GraphOptimizationLevel::ORT_ENABLE_ALL;
  }
  throw std::invalid_argument("Unknown ONNX optimization level");
}

} // namespace

OnnxruntimeInterpreter::OnnxruntimeInterpreter(
  const OnnxInterpreterParams& params,
  const std::string& path,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
)
  : params_(params)
  , session_(nullptr) {

  if (data_reader) {
    model_data_span_ = EnsureNotNull(data_reader->ReadPersistentData(path), path);
  } else {
    model_data_span_ = EnsureNotNull(ReadFileTo<IDataSpanPtr>(path), path);
  }
  const auto [model_data, model_data_size] = model_data_span_->Get();

  session_options_.SetGraphOptimizationLevel(ToOrtLevel(params_.optimization_level));

  if (params_.execution_mode == OnnxExecutionMode::PARALLEL) {
    session_options_.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
  } else {
    session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  }

  if (params_.use_ort_format) {
    session_options_.AddConfigEntry("session.load_model_format", "ORT");
  }

  if (data_reader) {
    if (const auto prefix = data_reader->GetPathPrefix()) {
      session_options_.AddConfigEntry("session.model_external_initializers_file_folder_path", prefix->c_str());
    }
  }

  if (params_.shrink_arena) {
    run_options_.AddConfigEntry("memory.enable_memory_arena_shrinkage", "cpu:0");
  }
  if (params_.disable_copy_to_ram) {
    session_options_.DisableCpuMemArena();
    session_options_.AddConfigEntry("session.use_ort_model_bytes_for_initializers", "1");
    session_options_.AddConfigEntry("session.use_ort_model_bytes_directly", "1");
    session_options_.AddConfigEntry("session.disable_prepacking", "1");
  }

  if (!params_.use_environment.empty()) {
    if (!environments) {
      throw std::runtime_error(
        "Model requests environment '" + params_.use_environment + "' but no environments are provided"
      );
    }
    auto it = environments->find(params_.use_environment);
    if (it == environments->end()) {
      throw std::runtime_error("Model environment not found: " + params_.use_environment);
    }

    auto onnx_env = std::dynamic_pointer_cast<OnnxEnvironment>(it->second);
    if (!onnx_env) {
      throw std::runtime_error("Model environment '" + params_.use_environment + "' is not a valid ONNX environment");
    }
    shared_env_ = std::move(onnx_env);
    session_options_.DisablePerSessionThreads();
    session_ = Ort::Session(shared_env_->env, model_data, model_data_size, session_options_);
  } else {
    Ort::ThreadingOptions thread_options;
    thread_options.SetGlobalInterOpNumThreads(params_.inter_op_num_threads);
    thread_options.SetGlobalIntraOpNumThreads(params_.intra_op_num_threads);
    env_ = Ort::Env(thread_options, ORT_LOGGING_LEVEL_WARNING, "giga-edge-onnx");
    session_options_.SetInterOpNumThreads(params_.inter_op_num_threads);
    session_options_.SetIntraOpNumThreads(params_.intra_op_num_threads);
    session_ = Ort::Session(env_, model_data, model_data_size, session_options_);
  }

  if (!params_.use_ort_format && !params_.disable_copy_to_ram) {
    model_data_span_.reset();
  }

  Init();
}

void OnnxruntimeInterpreter::Init() {
  size_t input_count = session_.GetInputCount();
  input_names_.resize(input_count);
  input_name_ptrs_.resize(input_count);
  input_buffers_.resize(input_count);
  input_tensors_.reserve(input_count);

  for (size_t i = 0; i < input_count; ++i) {
    auto name_ptr = session_.GetInputNameAllocated(i, allocator_);
    if (name_ptr.get()) {
      input_names_[i] = std::string(name_ptr.get());
      input_name_ptrs_[i] = input_names_[i].c_str();
      input_name_to_index_[input_names_[i]] = i;
    } else {
      throw std::runtime_error("Failed to get name for input " + std::to_string(i));
    }

    auto type_info = session_.GetInputTypeInfo(i);
    if (type_info.GetONNXType() != ONNX_TYPE_TENSOR) {
      throw std::runtime_error("Input '" + input_names_[i] + "' is not a tensor.");
    }

    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();

    for (auto& dim : shape) {
      if (dim == -1) {
        dim = 1;
      }
    }

    auto type = tensor_info.GetElementType();
    size_t element_size = onnx_utils::GetElementSize(type);
    size_t total_size = std::accumulate(shape.begin(), shape.end(), element_size, std::multiplies<size_t>());

    input_buffers_[i].resize(total_size);
    input_tensors_.push_back(
      Ort::Value::CreateTensor(
        allocator_.GetInfo(), input_buffers_[i].data(), input_buffers_[i].size(), shape.data(), shape.size(), type
      )
    );
  }

  size_t output_count = session_.GetOutputCount();
  output_names_.resize(output_count);
  output_name_ptrs_.resize(output_count);
  output_tensors_.reserve(output_count);
  for (size_t i = 0; i < output_count; ++i) {
    auto name_ptr = session_.GetOutputNameAllocated(i, allocator_);
    if (name_ptr.get()) {
      output_names_[i] = std::string(name_ptr.get());
      output_name_ptrs_[i] = output_names_[i].c_str();
      output_name_to_index_[output_names_[i]] = i;
    } else {
      throw std::runtime_error("Failed to get name for output " + std::to_string(i));
    }
    auto type_info = session_.GetOutputTypeInfo(i);
    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();
    for (auto& dim : shape) {
      if (dim == -1) {
        dim = 1; // use 1 for dynamic dimensions
      }
    }
    auto type = tensor_info.GetElementType();
    output_tensors_.push_back(Ort::Value::CreateTensor(allocator_, shape.data(), shape.size(), type));
  }
}

void OnnxruntimeInterpreter::Invoke() {
  output_tensors_ = session_.Run(
    run_options_,
    input_name_ptrs_.data(),
    input_tensors_.data(),
    input_tensors_.size(),
    output_name_ptrs_.data(),
    output_name_ptrs_.size()
  );
}

int OnnxruntimeInterpreter::GetInputId(const std::string& name) const {
  auto it = input_name_to_index_.find(name);
  if (it == input_name_to_index_.end()) {
    throw std::runtime_error("Input tensor not found: " + name);
  }
  return it->second;
}

int OnnxruntimeInterpreter::GetOutputId(const std::string& name) const {
  auto it = output_name_to_index_.find(name);
  if (it == output_name_to_index_.end()) {
    throw std::runtime_error("Output tensor not found: " + name);
  }
  return it->second;
}

TensorPtr OnnxruntimeInterpreter::Input(const std::string& name) const {
  return Input(GetInputId(name));
}

TensorPtr OnnxruntimeInterpreter::Output(const std::string& name) const {
  return Output(GetOutputId(name));
}

std::vector<std::string> OnnxruntimeInterpreter::InputNames() const {
  return input_names_;
}

std::vector<std::string> OnnxruntimeInterpreter::OutputNames() const {
  return output_names_;
}

TensorPtr OnnxruntimeInterpreter::Input(int id) const {
  if (id >= static_cast<int>(input_tensors_.size())) {
    return nullptr;
  }
  return std::make_unique<OnnxruntimeTensor>(const_cast<Ort::Value*>(&input_tensors_[id]), input_names_[id], id);
}

TensorPtr OnnxruntimeInterpreter::Output(int id) const {
  if (id >= static_cast<int>(output_tensors_.size())) {
    return nullptr;
  }
  return std::make_unique<OnnxruntimeTensor>(const_cast<Ort::Value*>(&output_tensors_[id]), output_names_[id], id);
}

int OnnxruntimeInterpreter::InputsCount() const {
  return input_tensors_.size();
}

int OnnxruntimeInterpreter::OutputsCount() const {
  return output_tensors_.size();
}

void OnnxruntimeInterpreter::ResizeInput(int id, const int* dims, int dims_count) {
  auto& tensor_value = input_tensors_[id];

  if (!tensor_value.IsTensor()) {
    throw std::runtime_error("Cannot resize input " + std::to_string(id) + " as it's not a valid tensor.");
  }

  auto tensor_info = tensor_value.GetTensorTypeAndShapeInfo();
  auto type = tensor_info.GetElementType();

  std::vector<int64_t> new_shape(dims, dims + dims_count);

  size_t element_size = onnx_utils::GetElementSize(type);
  size_t new_size = std::accumulate(new_shape.begin(), new_shape.end(), element_size, std::multiplies<size_t>());

  input_buffers_[id].resize(new_size);

  input_tensors_[id] = Ort::Value::CreateTensor(
    allocator_.GetInfo(), input_buffers_[id].data(), input_buffers_[id].size(), new_shape.data(), new_shape.size(), type
  );
}

void OnnxruntimeInterpreter::ResizeInput(const std::string& name, const int* dims, int dims_count) {
  ResizeInput(GetInputId(name), dims, dims_count);
}

void OnnxruntimeInterpreter::ReallocateTensors() {
  // For ONNX Runtime, reallocation is handled implicitly by the Run() call.
}

} // namespace giga_edge
