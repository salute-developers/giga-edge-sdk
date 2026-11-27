#include "giga_edge/modules/model/nodes/model/crnn_model.h"

#include "giga_edge/modules/model/models/crnn/crnn.h"
#include "giga_edge/modules/model/models/crnn/window_crnn.h"

namespace giga_edge {

CRNNModelNode::CRNNModelNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const CRNNModelNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {
  switch (params_.type) {
    case ModelType::STATEFUL_CRNN: {
      model_ = std::make_unique<StatefulCRNNModel>(params_.stateful_crnn_model_params, params_.path, data_reader);
      break;
    }
    case ModelType::WINDOW_CRNN: {
      model_ = std::make_unique<WindowCRNNModel>(params_.window_crnn_model_params, params_.path, data_reader);
      break;
    }
  }
}

void CRNNModelNode::ProcessOnce() {
  const auto& features = GetFeatures();

  std::vector<float> predictions;
  if (!features.empty()) {
    predictions = model_->PredictSingleStep(features.data(), features.size());
  }

  PushPredictions(std::move(predictions));
}

void CRNNModelNode::Reset() {
  model_->Reset();
}

GatherPredictionsNode::GatherPredictionsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void GatherPredictionsNode::ProcessOnce() {
  const auto& predictions = GetPredictionsBatch();

  std::vector<float> result;
  for (auto data : predictions) {
    result.insert(result.end(), data->begin(), data->end());
  }

  PushPredictions(std::move(result));
}

} // namespace giga_edge
