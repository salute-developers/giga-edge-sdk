#pragma once

#include "giga_edge/modules/model/models/crnn/base_crnn_model.h"

#include <memory>
#include <vector>

namespace giga_edge {

class StatefulCRNNModel final : public BaseCRNNModelImpl {
public:
  // If no data_reader is provided, path is the path on the file system.
  StatefulCRNNModel(
    const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr
  );

  ~StatefulCRNNModel() override = default;

  void Reset() override;

  std::vector<float> PredictSingleStep(const float* input, size_t num_elements) override;

protected:
  void CommonInit(int num_mels) override;
};

} // namespace giga_edge
