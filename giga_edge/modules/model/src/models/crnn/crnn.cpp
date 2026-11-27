#include "giga_edge/modules/model/models/crnn/crnn.h"

#include <stdexcept>

namespace giga_edge {

static const int kLSTMCellSize = 256;
static const std::string kInputsIndicesInputTensor = "input_ph";
static const std::string kInputsIndicesCellStateH = "inference/cell_state_h";
static const std::string kInputsIndicesCellStateC = "inference/cell_state_c";
static const std::string kOutputsIndicesOutputTensor = "inference/Sigmoid";
static const std::string kOutputsIndicesCellStateH = "inference/rnn/cell1/Mul_5";
static const std::string kOutputsIndicesCellStateC = "inference/rnn/cell1/Add_3";

StatefulCRNNModel::StatefulCRNNModel(
  const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader
)
  : BaseCRNNModelImpl(params, path, data_reader) {
  CommonInit(params.num_mels);
  Reset();
}

void StatefulCRNNModel::Reset() {
  auto lstm_state_h = interpreter_->InputFloatData(kInputsIndicesCellStateH);
  auto lstm_state_c = interpreter_->InputFloatData(kInputsIndicesCellStateC);
  for (size_t i = 0; i < kLSTMCellSize; i++) {
    lstm_state_h[i] = 0;
    lstm_state_c[i] = 0;
  }
}

std::vector<float> StatefulCRNNModel::PredictSingleStep(const float* input, size_t num_elements) {
  if (num_elements != input_tensor_size_) {
    throw std::runtime_error("Number of elements in input data should be equal tensor size");
  }

  auto input_tensor_data = interpreter_->InputFloatData(kInputsIndicesInputTensor);
  std::copy(input, input + num_elements, input_tensor_data);

  interpreter_->Invoke();
  auto predictions = interpreter_->OutputFloatData(kOutputsIndicesOutputTensor);

  auto input_lstm_state_h = interpreter_->InputFloatData(kInputsIndicesCellStateH);
  auto input_lstm_state_c = interpreter_->InputFloatData(kInputsIndicesCellStateC);
  auto output_lstm_state_h = interpreter_->OutputFloatData(kOutputsIndicesCellStateH);
  auto output_lstm_state_c = interpreter_->OutputFloatData(kOutputsIndicesCellStateC);

  for (int i = 0; i < kLSTMCellSize; ++i) {
    input_lstm_state_h[i] = output_lstm_state_h[i];
    input_lstm_state_c[i] = output_lstm_state_c[i];
  }

  return {predictions, predictions + output_tensor_size_};
}

void StatefulCRNNModel::CommonInit(int num_mels) {
  auto input = interpreter_->Input(kInputsIndicesInputTensor);
  auto output = interpreter_->Output(kOutputsIndicesOutputTensor);

  if (input->DimsNum() != 3 || input->Dim(0) != 1 || input->Dim(2) != num_mels) {
    throw std::runtime_error("Unexpected dimension of model tensors");
  }

  input_tensor_size_ = input->Dim(1) * input->Dim(2);
  output_tensor_size_ = 1;
  for (int i = 0; i < output->DimsNum(); ++i) {
    output_tensor_size_ *= output->Dim(i);
  }
}

} // namespace giga_edge
