#pragma once

#include "giga_edge/modules/model/models/crnn/base_crnn_model.h"
#include "giga_edge/modules/model/models/crnn/params.h"

#include <memory>
#include <vector>

namespace giga_edge {

class CRNNModelStep final : public BaseCRNNModelImpl {
public:
  // If no data_reader is provided, path is the path on the file system.
  CRNNModelStep(const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr);

  ~CRNNModelStep() override = default;

  void Reset() override;

  std::vector<float> PredictSingleStep(const float* input, size_t num_elements) override;

protected:
  void CommonInit(int num_mels) override;
};

class CRNNModelSlidingWindow {
public:
  explicit CRNNModelSlidingWindow(const CRNNSlidingWindowParams& params);

  virtual ~CRNNModelSlidingWindow() = default;

  void Reset();

  std::vector<float> GetPredictions(const std::vector<float>& predictions);

private:
  CRNNSlidingWindowParams params_;

  std::vector<float> stashed_predictions_;
};

class WindowCRNNModel : public IBaseCRNNModel {
public:
  // If no data_reader is provided, path is the path on the file system.
  WindowCRNNModel(
    const WindowCRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader = nullptr
  );

  void Reset() override;

  std::vector<float> PredictSingleStep(const float* input, size_t num_elements) override;

  size_t GetInputSize() override {
    return model_step_.GetInputSize();
  }

  size_t GetOutputSize() override {
    return model_step_.GetOutputSize();
  }

private:
  CRNNModelStep model_step_;
  CRNNModelSlidingWindow sliding_window_;
};

} // namespace giga_edge
