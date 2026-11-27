#include "giga_edge/modules/model/models/crnn/window_crnn.h"

#include <cassert>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace giga_edge {

static const std::string kInputsIndicesInputTensor = "input";
static const std::string kOutputsIndicesOutputTensor = "Identity";

CRNNModelStep::CRNNModelStep(const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader)
  : BaseCRNNModelImpl(params, path, data_reader) {
  CommonInit(params.num_mels);
}

void CRNNModelStep::Reset() {
  for (int i = 1; i < interpreter_->InputsCount(); ++i) {
    auto tensor = interpreter_->Input(i);
    std::memset(tensor->Data(), 0, tensor->Size());
  }
}

std::vector<float> CRNNModelStep::PredictSingleStep(const float* input, size_t num_elements) {
  if (num_elements != input_tensor_size_) {
    throw std::runtime_error("Number of elements in input data should be equal tensor size");
  }

  auto input_tensor_data = interpreter_->InputFloatData(kInputsIndicesInputTensor);
  std::copy(input, input + num_elements, input_tensor_data);

  interpreter_->Invoke();

  for (int i = 1; i < interpreter_->InputsCount(); ++i) {
    auto tensor_input = interpreter_->Input(i);
    auto tensor_output = interpreter_->Output(i);
    assert(tensor_input->Size() == tensor_output->Size());
    if (auto dst = tensor_input->Data(), src = tensor_output->Data(); dst != src) {
      std::memcpy(dst, src, tensor_output->Size());
    }
  }

  auto output = interpreter_->Output(kOutputsIndicesOutputTensor);
  auto predictions = output->FloatData();

  if (output_tensor_size_ == 0) {
    output_tensor_size_ = 1;
    for (int i = 0; i < output->DimsNum(); ++i) {
      output_tensor_size_ *= output->Dim(i);
    }
  }

  return {predictions, predictions + output_tensor_size_};
}

void CRNNModelStep::CommonInit(int num_mels) {
  auto input = interpreter_->Input(kInputsIndicesInputTensor);

  if (input->DimsNum() != 3 || input->Dim(0) != 1 || input->Dim(2) != num_mels) {
    throw std::runtime_error("Unexpected dimension of model tensors");
  }

  input_tensor_size_ = input->Dim(1) * input->Dim(2);

  Reset();
}

CRNNModelSlidingWindow::CRNNModelSlidingWindow(const CRNNSlidingWindowParams& params)
  : params_(params) {}

std::vector<float> CRNNModelSlidingWindow::GetPredictions(const std::vector<float>& predictions) {
  stashed_predictions_.insert(stashed_predictions_.end(), predictions.begin(), predictions.end());

  if (static_cast<int>(stashed_predictions_.size()) < params_.window_size * params_.num_classes) {
    return {};
  }

  int num_aggregated_predictions = static_cast<int>(stashed_predictions_.size())
    - (params_.window_size - 1) * params_.num_classes;
  auto window_size_float = static_cast<float>(params_.window_size);
  std::vector<float> result(num_aggregated_predictions);
  for (int i = 0; i < num_aggregated_predictions; i += params_.num_classes) {
    // Mean of window_size elements.
    for (int j = 0; j < params_.num_classes; ++j) {
      int index = i + j;
      result[index] = 0.0;
      for (int window_index = 0; window_index < params_.window_size; ++window_index) {
        result[index] += stashed_predictions_[index + params_.num_classes * window_index];
      }
      result[index] /= window_size_float;
    }
  }

  stashed_predictions_.erase(stashed_predictions_.begin(), stashed_predictions_.begin() + num_aggregated_predictions);
  return result;
}

void CRNNModelSlidingWindow::Reset() {
  stashed_predictions_.clear();
}

WindowCRNNModel::WindowCRNNModel(
  const WindowCRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader
)
  : model_step_(params.step_params, path, data_reader)
  , sliding_window_(params.window_params) {}

void WindowCRNNModel::Reset() {
  model_step_.Reset();
  sliding_window_.Reset();
}

std::vector<float> WindowCRNNModel::PredictSingleStep(const float* input, size_t num_elements) {
  auto predictions = model_step_.PredictSingleStep(input, num_elements);
  return sliding_window_.GetPredictions(predictions);
}

} // namespace giga_edge
