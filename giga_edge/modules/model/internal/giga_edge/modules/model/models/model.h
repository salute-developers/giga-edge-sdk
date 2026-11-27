#pragma once

#include "giga_edge/lib/common/span.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/model/interpreter/tensor.h"
#include "giga_edge/modules/model/models/model_detail.h"
#include "giga_edge/modules/model/models/model_params.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace giga_edge {

class Model {
public:
  using TensorType = Tensor::DataType;
  using TensorData = model::detail::TensorData;

  struct StateTensor {
    std::vector<std::byte> data;
    const std::vector<int> shape;
  };

  template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  struct TensorTypeSelector {
    using Type = T;
  };

  template<typename F, typename... Args>
  static constexpr decltype(auto) InvokeWithTensorType(TensorType type, F&& f, Args&&... args) {
    switch (type) {
      case Tensor::DataType::FLOAT32:
        return std::forward<F>(f)(TensorTypeSelector<float>{}, std::forward<Args>(args)...);
      case Tensor::DataType::INT32:
        return std::forward<F>(f)(TensorTypeSelector<int>{}, std::forward<Args>(args)...);
      case Tensor::DataType::INT64:
        return std::forward<F>(f)(TensorTypeSelector<int64_t>{}, std::forward<Args>(args)...);
    }
    throw std::out_of_range("Unknown tensor type enum value: " + std::to_string(static_cast<int>(type)));
  }

public:
  Model() = default;

  // If no data_reader is provided, path is the path on the file system.
  Model(
    const ModelParams& model_params,
    const std::string& path,
    const IDataReaderPtr& data_reader = nullptr,
    const std::map<std::string, EnvironmentPtr>* environments = nullptr
  );

  virtual ~Model() = default;

  void Reset();

  void Invoke();

  int InputsCount() const;
  int OutputsCount() const;
  int StatesCount() const;

  // Special all-zeroes-span Span{nullptr, size} values are supported in Set* methods.

  template<typename SpanLike>
  void SetInputData(const std::vector<SpanLike>& data, const int start_index = 0) {
    for (int i = 0; i < static_cast<int>(data.size()); ++i) {
      auto index = start_index + i;
      auto tensor = interpreter_->Input(index);
      std::string name(tensor->GetName());
      CheckIfMutableInput(index, name);
      LazySetInputTensor(*tensor, index, name, data[i]);
    }
  }

  template<typename StringLike, typename SpanLike>
  void SetInputData(const std::unordered_map<StringLike, SpanLike>& data_map) {
    LazySetInputData(data_map, true /* reject const inputs */);
  }

  template<class OutputT>
  std::unordered_map<std::string, std::vector<OutputT>> GetOutputData(const std::vector<std::string>& outputs) const {
    std::unordered_map<std::string, std::vector<OutputT>> data_map;
    for (const auto& output : outputs) {
      auto tensor = interpreter_->Output(output);
      data_map[output] = GetTensorData<OutputT>(*tensor);
    }
    return data_map;
  }
  template<class OutputT>
  std::unordered_map<std::string, std::vector<OutputT>> GetOutputData() const {
    return GetOutputData<OutputT>(GetOutputNames());
  }

  std::unordered_map<std::string, TensorData> GetOutputData(const std::vector<std::string>& outputs) const;
  std::unordered_map<std::string, TensorData> GetOutputData() const;

  template<class OutputT = float>
  std::vector<std::vector<OutputT>> GetOutputData(int start, int count = 0) const {
    if (count == 0) {
      count = interpreter_->OutputsCount() - states_count_ - start;
    }

    std::vector<std::vector<OutputT>> result;
    for (int i = 0; i < count; ++i) {
      auto tensor = interpreter_->Output(start + i);
      result.push_back(std::move(GetTensorData<OutputT>(*tensor)));
    }
    return result;
  }

  std::vector<StateTensor> GetState() const;
  void SetState(const std::vector<StateTensor>& data);
  void ResetState();

  std::vector<std::string> GetInputNames() const;
  std::vector<std::string> GetOutputNames() const;
  std::vector<TensorType> GetInputTypes() const;
  std::vector<TensorType> GetOutputTypes() const;

  std::vector<int> GetInputsNumElements() const;

  void ResizeInput(const std::string& name, const std::vector<int>& shape);

private:
  template<class DataT>
  static void SetTensorData(Tensor& tensor, const DataT* data, int data_size) {
    int tensor_size;
    if constexpr (std::is_same_v<std::remove_cv_t<DataT>, std::byte>) {
      tensor_size = tensor.Size(); // untyped tensor
    } else {
      tensor_size = tensor.NumElements();
    }

    if (data_size != tensor_size) {
      throw std::runtime_error(
        "Tensor '" + std::string(tensor.GetName()) + "' input size mismatch. Expected " + std::to_string(tensor_size)
        + ", provided " + std::to_string(data_size)
      );
    }

    if (data_size == 0) { // tflite returns nullptr as buffer address of zero-sized tensors
      return;
    }

    if constexpr (std::is_same_v<std::remove_cv_t<DataT>, std::byte>) {
      FillTensorBuffer<DataT>(tensor, data, data_size);
    } else {
      InvokeWithTensorType(tensor.GetDataType(), [&tensor, data, data_size](auto type_sel) {
        using TensorT = typename decltype(type_sel)::Type;
        FillTensorBuffer<TensorT>(tensor, data, data_size);
      });
    }
  }

  template<class TensorT, class DataT>
  static void FillTensorBuffer(Tensor& tensor, const DataT* data, int data_size) {
    auto buf = GetTensorBuffer<TensorT>(tensor);
    if (data == nullptr) { // special zeroing range
      std::fill(buf, buf + data_size, static_cast<TensorT>(0));
    } else if constexpr (std::is_same_v<std::remove_cv_t<DataT>, TensorT>) {
      std::copy(data, data + data_size, buf);
    } else {
      std::transform(data, data + data_size, buf, [](DataT value) {
        return static_cast<TensorT>(value);
      });
    }
  }

  template<typename... Ts>
  static void SetTensorData(Tensor& tensor, const std::variant<Ts...>& data) {
    std::visit(
      [&tensor](const auto& value) {
        SetTensorData(tensor, value);
      },
      data
    );
  }

  template<typename AnyData>
  static auto SetTensorData(Tensor& tensor, const AnyData& data)
    -> std::void_t<decltype(model::detail::GetDataSize(data))> {
    model::detail::ApplyToSpan(data, [&tensor](const auto& span) {
      SetTensorData(tensor, span.data(), span.size());
    });
  }

  template<typename DataT>
  static DataT* GetTensorBuffer(Tensor& tensor) {
    DataT* tensor_data = nullptr;
    if constexpr (std::is_same_v<DataT, float>) {
      tensor_data = tensor.FloatData();
    } else if constexpr (std::is_same_v<DataT, int32_t>) {
      tensor_data = tensor.IntData();
    } else if constexpr (std::is_same_v<DataT, std::byte>) {
      tensor_data = reinterpret_cast<std::byte*>(tensor.Data());
    } else if constexpr (std::is_same_v<DataT, int64_t>) {
      tensor_data = tensor.Int64Data();
    } else {
      static_assert(sizeof(DataT) == 0, "Unsupported data type");
    }
    if (!tensor_data) {
      throw std::runtime_error("Tensor '" + std::string(tensor.GetName()) + "' input data type mismatch");
    }
    return tensor_data;
  }

  template<class DataT>
  static std::vector<DataT> GetTensorData(Tensor& tensor) {
    if constexpr (std::is_same_v<std::remove_cv_t<DataT>, std::byte>) {
      auto tensor_data = GetTensorBuffer<DataT>(tensor);
      return {tensor_data, tensor_data + tensor.Size()};
    } else {
      return InvokeWithTensorType(tensor.GetDataType(), [&tensor](auto type_sel) {
        using TensorT = typename decltype(type_sel)::Type;
        auto tensor_data = GetTensorBuffer<TensorT>(tensor);
        return std::vector<DataT>(tensor_data, tensor_data + tensor.NumElements());
      });
    }
  }

  static TensorData GetTensorData(Tensor& tensor);

  template<typename StringLike, typename AnyData>
  void LazySetInputTensor(Tensor& tensor, int index, const StringLike& name, AnyData&& data) {
    if (ResizeInputTensor(tensor, name, model::detail::GetDataSize(data))) {
      deferred_inputs_.is_reallocation_needed = true;
    }

    if (
      deferred_inputs_.is_reallocation_needed
      || (IsResizingLikely() && model::detail::IsLightweightDeferrable(std::forward<AnyData>(data)))
    ) {
      deferred_inputs_.inputs.resize(InputsCount());
      // NOLINTNEXTLINE(bugprone-use-after-move): IsLightweightDeferrable() inspects only, never moves
      deferred_inputs_.inputs[index] = model::detail::Defer(std::forward<AnyData>(data));
    } else {
      SetTensorData(tensor, data);
    }
  }

  template<typename StringLike, typename SpanLike>
  void LazySetInputData(const std::unordered_map<StringLike, SpanLike>& data_map, const bool only_mutable) {
    for (const auto& [name, data] : data_map) {
      auto tensor = interpreter_->Input(name);
      auto index = tensor->GetIndex();
      if (only_mutable) {
        CheckIfMutableInput(index, name);
      }
      LazySetInputTensor(*tensor, index, name, data);
    }
  }

  bool IsResizingLikely() const {
    return !params_.mutable_dims.empty();
  }
  // Returns false if resizing is not needed.
  bool ResizeInputTensor(Tensor& tensor, const std::string& name, int data_size);
  template<typename GetShape>
  void ResizeRecursiveState(GetShape get_shape);
  void CheckIfMutableInput(int index, const std::string& name) const;

  std::vector<StateTensor> GetState(bool is_input) const;
  void PrepareRecursiveStates();
  void ApplyDefferedState();
  void ApplyDefferedStateNoReallocPath();
  void BackupTensors(const std::vector<TensorPtr>& input_tensors);

  struct DeferredInputState {
    void Reset() {
      inputs.clear();
      states.clear();
      is_reallocation_needed = false;
      transfer_output_to_input = true;
    }

    template<typename T>
    std::enable_if_t<std::is_same_v<std::decay_t<T>, std::vector<StateTensor>>> AssignState(T&& src) {
      const std::size_t count = src.size();
      states.clear();
      states.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
        if constexpr (std::is_reference_v<T>) { // protect against moving a non-const lvalue
          states.push_back(src[i].data);
        } else {
          states.push_back(std::move(src[i].data));
        }
      }
    }

    std::vector<std::optional<TensorData>> inputs;
    std::vector<TensorData> states;
    bool is_reallocation_needed = false;
    // If transfer_output_to_input == true, the recursive state input tensors should be initialized
    // from the corresponding output tensors. The flag is set right after Invoke if the recursive state
    // is not resized on this iteration and can be reset by Set/Reset state operations.
    bool transfer_output_to_input = false;
  };

private:
  ModelParams params_;
  int inputs_count_;
  int states_count_;
  InterpreterPtr interpreter_;
  DeferredInputState deferred_inputs_;
  std::vector<std::vector<int>> initial_shapes_;
};

} // namespace giga_edge
