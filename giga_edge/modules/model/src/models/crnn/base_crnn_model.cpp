#include "giga_edge/modules/model/models/crnn/base_crnn_model.h"

#include "giga_edge/lib/common/str_utils.h"
#include "giga_edge/lib/version/version.h"

#include <cassert>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace giga_edge {

BaseCRNNModelImpl::BaseCRNNModelImpl(
  const CRNNModelParams& params, const std::string& path, const IDataReaderPtr& data_reader
)
  : input_tensor_size_(0)
  , output_tensor_size_(0) {
  interpreter_ = MakeInterpreter(params.interpreter_params, path, data_reader);
}

size_t BaseCRNNModelImpl::GetInputSize() {
  return input_tensor_size_;
}

size_t BaseCRNNModelImpl::GetOutputSize() {
  return output_tensor_size_;
}

} // namespace giga_edge
