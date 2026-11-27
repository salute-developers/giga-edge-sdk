#include "giga_edge/modules/model/models/model.h"

#include "giga_edge/lib/enum/enum_fwd.h"
#include "giga_edge/modules/model/interpreter/tensor.h"
#include "giga_edge/modules/model/models/model_params.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <variant>

namespace giga_edge {

namespace {

std::string GetTensorId(int idx, const std::string& name) {
  auto result = std::to_string(idx);
  result.reserve(result.size() + name.size() + 3);
  ((result += " '") += name) += '\'';
  return result;
}

std::string GetTensorId(const Tensor& t) {
  return GetTensorId(t.GetIndex(), std::string(t.GetName()));
}

bool AreShapesEqual(const Tensor& a, const std::vector<int>& b) {
  const int count = a.DimsNum();
  if (count != b.size()) {
    return false;
  }

  for (int i = 0; i < count; ++i) {
    if (a.Dim(i) != b[i]) {
      return false;
    }
  }

  return true;
}

void ProcessRecursiveStates(
  Interpreter& interpreter,
  const ModelParams& params,
  int input_idx,
  int output_idx,
  int state_count,
  std::vector<std::vector<int>>& shapes
) {
  shapes.reserve(state_count);
  for (int i = 0; i < state_count; ++i, ++input_idx, ++output_idx) {
    auto input = interpreter.Input(input_idx);
    auto output = interpreter.Output(output_idx);

    std::string input_name(input->GetName());
    bool overridden = false;

    for (const auto& [prefix, override_shape] : params.initial_state_shape_overrides) {
      if (input_name.rfind(prefix, 0) == 0) {
        shapes.push_back(override_shape);
        overridden = true;
        break;
      }
    }

    if (!overridden) {
      shapes.push_back(input->GetShape());
    }

    if (!input->IsSameType(*output)) {
      throw std::runtime_error(
        std::string("The data type of the rucursive state ") + std::to_string(i) + " input tensor "
        + GetTensorId(*input) + " differs from the data type of the corresponding output tensor " + GetTensorId(*output)
      );
    }
  }
}

void ValidateMutableDims(Interpreter& interpreter, const ModelParams& model_params) {
  for (const auto& input_dim : model_params.mutable_dims) {
    auto tensor = interpreter.Input(input_dim.first);
    if ((input_dim.second < 0) || (input_dim.second >= tensor->DimsNum())) {
      throw std::runtime_error(
        "The mutable dimension index " + std::to_string(input_dim.second) + " of the input '" + input_dim.first
        + "' is out of bounds [0, " + std::to_string(tensor->DimsNum()) + ")"
      );
    }
  }
}

} // namespace

Model::Model(
  const ModelParams& model_params,
  const std::string& path,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
)
  : params_(model_params) {
  interpreter_ = MakeInterpreter(params_.interpreter, path, data_reader, environments);
  states_count_ = params_.inputs_count >= 0 ? interpreter_->InputsCount() - params_.inputs_count : 0;
  if (states_count_ < 0) {
    throw std::runtime_error(
      "Model config input count " + std::to_string(params_.inputs_count) + " is greater than actual "
      + std::to_string(interpreter_->InputsCount())
    );
  }

  inputs_count_ = interpreter_->InputsCount() - states_count_;
  ProcessRecursiveStates(*interpreter_, params_, InputsCount(), OutputsCount(), states_count_, initial_shapes_);
  ValidateMutableDims(*interpreter_, params_);

  Reset();
  LazySetInputData(params_.const_inputs, false);
}

int Model::InputsCount() const {
  return inputs_count_;
}

int Model::OutputsCount() const {
  return interpreter_->OutputsCount() - states_count_;
}

int Model::StatesCount() const {
  return states_count_;
}

void Model::Reset() {
  ResetState();
  // The inputs are left intact.
}

void Model::Invoke() {
  ApplyDefferedState();
  deferred_inputs_.Reset();
  interpreter_->Invoke();
  PrepareRecursiveStates();
}

void Model::ResizeInput(const std::string& name, const std::vector<int>& shape) {
  auto tensor = interpreter_->Input(name);
  if (!AreShapesEqual(*tensor, shape)) {
    interpreter_->ResizeInput(name, shape.data(), shape.size());
    deferred_inputs_.is_reallocation_needed = true;
  }
}

// Returns false if resizing is not needed.
bool Model::ResizeInputTensor(Tensor& tensor, const std::string& name, int data_size) {
  int tensor_size = tensor.NumElements();
  if (data_size == tensor_size) {
    return false;
  }

  const auto found = params_.mutable_dims.find(name);
  if (found == params_.mutable_dims.end()) {
    throw std::runtime_error(
      "Tensor '" + name + "' input size mismatch. Expected " + std::to_string(tensor_size) + ", but provided "
      + std::to_string(data_size)
    );
  }

  auto shape = tensor.GetShape();
  auto stride = tensor_size / shape[found->second]; // safe because params are checked in the constructor
  shape[found->second] = data_size / stride;
  if (data_size % stride) {
    throw std::runtime_error(
      "Tensor '" + name + "' input size " + std::to_string(data_size) + " is not a multiple of its stride "
      + std::to_string(stride)
    );
  }

  interpreter_->ResizeInput(name, shape.data(), shape.size());
  return true;
}

void Model::CheckIfMutableInput(int index, const std::string& name) const {
  constexpr auto throw_error = [](const std::string& type, int index, const std::string& name) {
    throw std::out_of_range(type + " input tensor " + GetTensorId(index, name) + " is passed to SetInputData()");
  };

  if (index >= InputsCount()) {
    throw_error("Recurrent state", index, name);
  }

  if (params_.const_inputs.find(name) != params_.const_inputs.cend()) {
    throw_error("Constant", index, name);
  }
}

std::vector<Model::StateTensor> Model::GetState(bool is_input) const {
  std::vector<StateTensor> result;
  result.reserve(states_count_);
  const int base = is_input ? InputsCount() : OutputsCount();
  using GetTensor = TensorPtr (Interpreter::*)(int) const;
  const auto get_tensor = is_input
    ? static_cast<GetTensor>(&Interpreter::Input)
    : static_cast<GetTensor>(&Interpreter::Output);
  for (int i = 0; i < states_count_; ++i) {
    auto tensor = (interpreter_.get()->*get_tensor)(base + i);
    auto data = reinterpret_cast<std::byte*>(tensor->Data());
    result.push_back({{data, data + tensor->Size()}, tensor->GetShape()});
  }
  return result;
}

std::vector<Model::StateTensor> Model::GetState() const {
  return GetState(false /* output */);
}

template<typename GetShape>
void Model::ResizeRecursiveState(GetShape get_shape) {
  for (int input_base = InputsCount(), i = 0; i < states_count_; ++i) {
    const auto& shape = get_shape(i);
    if (!AreShapesEqual(*interpreter_->Input(input_base + i), shape)) {
      interpreter_->ResizeInput(input_base + i, shape.data(), shape.size());
      deferred_inputs_.is_reallocation_needed = true;
    }
  }
}

void Model::SetState(const std::vector<Model::StateTensor>& data) {
  if (data.size() != states_count_) {
    throw std::runtime_error(
      "Invalid recursive state count: " + std::to_string(states_count_) + " expected, but "
      + std::to_string(data.size()) + " provided"
    );
  }

  ResizeRecursiveState([&data](auto i) -> const auto& {
    return data[i].shape;
  });

  if (deferred_inputs_.is_reallocation_needed) {
    deferred_inputs_.AssignState(data);
  } else {
    for (int input_base = InputsCount(), i = 0; i < states_count_; ++i) {
      SetTensorData(*interpreter_->Input(input_base + i), data[i].data);
    }
  }

  deferred_inputs_.transfer_output_to_input = false;
}

void Model::ResetState() {
  assert(initial_shapes_.size() == states_count_);
  ResizeRecursiveState([&shapes = initial_shapes_](auto i) -> const auto& {
    return shapes[i];
  });

  // Use special zeroing spans as deferred data.
  deferred_inputs_.states.clear();
  deferred_inputs_.states.reserve(states_count_);
  for (int input_base = InputsCount(), i = 0; i < states_count_; ++i) {
    auto tensor = interpreter_->Input(input_base + i);
    deferred_inputs_.states.emplace_back(Span<const std::byte>(nullptr /* zeroing */, tensor->Size()));
  }

  deferred_inputs_.transfer_output_to_input = false;
}

std::vector<std::string> Model::GetInputNames() const {
  std::vector<std::string> inputs = interpreter_->InputNames();
  inputs.resize(InputsCount());
  return inputs;
}

std::vector<std::string> Model::GetOutputNames() const {
  std::vector<std::string> outputs = interpreter_->OutputNames();
  outputs.resize(OutputsCount());
  return outputs;
}

std::vector<Model::TensorType> Model::GetInputTypes() const {
  const int count = InputsCount();
  std::vector<Model::TensorType> types(count);
  for (int i = 0; i < count; ++i) {
    types[i] = interpreter_->Input(i)->GetDataType();
  }
  return types;
}
std::vector<Model::TensorType> Model::GetOutputTypes() const {
  const int count = OutputsCount();
  std::vector<Model::TensorType> types(count);
  for (int i = 0; i < count; ++i) {
    types[i] = interpreter_->Output(i)->GetDataType();
  }
  return types;
}

std::vector<int> Model::GetInputsNumElements() const {
  const int input_count = InputsCount();
  std::vector<int> num_elements(input_count);
  for (int i = 0; i < input_count; ++i) {
    num_elements[i] = interpreter_->Input(i)->NumElements();
  }
  return num_elements;
}

Model::TensorData Model::GetTensorData(Tensor& tensor) {
  return InvokeWithTensorType(
    tensor.GetDataType(),
    [](auto type_sel, auto& tensor) -> TensorData {
      using DataType = typename decltype(type_sel)::Type;
      auto data = GetTensorBuffer<DataType>(tensor);
      return std::vector<DataType>(data, data + tensor.NumElements());
    },
    tensor
  );
}

std::unordered_map<std::string, Model::TensorData> Model::GetOutputData(const std::vector<std::string>& outputs) const {
  std::unordered_map<std::string, TensorData> data_map;
  for (const auto& output : outputs) {
    auto tensor = interpreter_->Output(output);
    data_map[output] = GetTensorData(*tensor);
  }
  return data_map;
}
std::unordered_map<std::string, Model::TensorData> Model::GetOutputData() const {
  return GetOutputData(GetOutputNames());
}

void Model::PrepareRecursiveStates() {
  if (0 == states_count_) {
    return;
  }

  std::vector<int> shape;
  shape.reserve(4); // ok for most cases
  for (int input_base = InputsCount(), output_base = OutputsCount(), i = 0; i < states_count_; ++i) {
    auto in = interpreter_->Input(input_base + i);
    auto out = interpreter_->Output(output_base + i);
    out->GetShape(shape);
    if (!AreShapesEqual(*in, shape)) {
      interpreter_->ResizeInput(input_base + i, shape.data(), shape.size());
      deferred_inputs_.is_reallocation_needed = true;
    }
  }

  if (deferred_inputs_.is_reallocation_needed) {
    // At this point all input state tensors are resized, no need to store their shapes.
    deferred_inputs_.AssignState(GetState(false));
    deferred_inputs_.transfer_output_to_input = false;
  }
}

void Model::ApplyDefferedStateNoReallocPath() {
  // Recursive state.
  if (!deferred_inputs_.states.empty()) {
    assert(deferred_inputs_.states.size() == states_count_);
    for (int input_base = InputsCount(), i = 0; i < states_count_; ++i) {
      SetTensorData(*interpreter_->Input(input_base + i), deferred_inputs_.states[i]);
    }
  } else if (deferred_inputs_.transfer_output_to_input) {
    // Move recursive state output to input.
    for (int input_base = InputsCount(), output_base = OutputsCount(), i = 0; i < states_count_; ++i) {
      auto tensor_input = interpreter_->Input(input_base + i);
      auto tensor_output = interpreter_->Output(output_base + i);
      if (auto dst = tensor_input->Data(), src = tensor_output->Data(); dst != src) {
        std::memcpy(dst, src, tensor_output->Size());
      }
    }
  }

  // Possible deferred lazy inputs.
  for (std::size_t i = 0; i < deferred_inputs_.inputs.size(); ++i) {
    if (deferred_inputs_.inputs[i].has_value()) {
      SetTensorData(*interpreter_->Input(i), *deferred_inputs_.inputs[i]);
    }
  }
}

void Model::BackupTensors(const std::vector<TensorPtr>& input_tensors) {
  assert(input_tensors.size() >= InputsCount());

  // Back up recursive state tensors.
  if (states_count_ && deferred_inputs_.states.empty()) {
    if (deferred_inputs_.transfer_output_to_input) {
      deferred_inputs_.AssignState(GetState(false));
    } else {
      deferred_inputs_.AssignState(Model::GetState(true /* input */));
    }
  }

  // Back up input tensors paying attention to constant inputs.
  const int input_count = InputsCount();
  auto& inputs = deferred_inputs_.inputs;
  inputs.resize(input_count);
  for (int i = 0; i < input_count; ++i) {
    if (!inputs[i].has_value()) {
      auto found = params_.const_inputs.find(std::string(input_tensors[i]->GetName()));
      if (found == params_.const_inputs.end()) {
        inputs[i] = GetTensorData(*input_tensors[i]);
      } else {
        std::visit(
          [&inputs, i](const auto& vec) {
            inputs[i] = Span(vec.data(), vec.size());
          },
          found->second
        );
      }
    }
  }
}

void Model::ApplyDefferedState() {
  if (!deferred_inputs_.is_reallocation_needed) {
    ApplyDefferedStateNoReallocPath();
    return;
  }

  std::vector<TensorPtr> input_tensors;
  input_tensors.reserve(interpreter_->InputsCount());
  for (auto count = interpreter_->InputsCount(), i = 0; i < count; ++i) {
    input_tensors.push_back(interpreter_->Input(i));
  }

  BackupTensors(input_tensors);

  interpreter_->ReallocateTensors();

  // Restore input tensors.
  const int input_count = InputsCount();
  assert(deferred_inputs_.inputs.size() == input_count);
  for (int i = 0; i < input_count; ++i) {
    SetTensorData(*input_tensors[i], *deferred_inputs_.inputs[i]);
  }

  // Restore recursive state tensors.
  assert(deferred_inputs_.states.size() == states_count_);
  for (int i = 0; i < states_count_; ++i) {
    SetTensorData(*input_tensors[input_count + i], deferred_inputs_.states[i]);
  }
}

} // namespace giga_edge
