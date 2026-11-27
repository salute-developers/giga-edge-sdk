#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/model/models/crnn/base_crnn_model.h"
#include "giga_edge/modules/model/models/crnn/window_crnn.h"
#include "giga_edge/modules/model/nodes/model/crnn_model_params.h"

namespace giga_edge {
class CRNNModelNode : public NodeBase {
public:
  CRNNModelNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const CRNNModelNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(crnn_model, features, Features, std::vector<float>, "Features for model to apply");
  REGISTER_OUTPUT(crnn_model, predictions, Predictions, std::vector<float>, "Model predictions");

private:
  CRNNModelNodeParams params_;
  std::unique_ptr<IBaseCRNNModel> model_;
};

REGISTER_NODE(crnn_model, CRNNModelNode, CRNNModelNodeParams, true, "Apply CRNN model");

class GatherPredictionsNode : public NodeBase {
public:
  explicit GatherPredictionsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_BATCH_INPUT(gather_predictions, predictions, Predictions, std::vector<float>, "Predictions to gather");
  REGISTER_OUTPUT(gather_predictions, predictions, Predictions, std::vector<float>, "Gathered predictions");
};

REGISTER_NODE(gather_predictions, GatherPredictionsNode, "Gathers batch of predictions into one vector");
DEPRECATE_NODE(gather_predictions, "Use batch_gather instead")

} // namespace giga_edge
