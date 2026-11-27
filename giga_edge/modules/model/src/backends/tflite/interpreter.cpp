#include "giga_edge/modules/model/backends/tflite/interpreter.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/model/backends/tflite/op_unpack_5bit.h"
#include "giga_edge/modules/model/backends/tflite/tensor.h"

#ifdef GIGA_EDGE_TFLITE_XNNPACK
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#endif

#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

template<typename GetFunc>
std::pair<const ::TfLiteTensor*, int> GetTensor(
  const std::string& name,
  const std::string& kind,
  ::TfLiteInterpreter* interpreter,
  GetFunc* get_tensor,
  const int count
) {
  for (int index = 0; index < count; ++index) {
    auto tensor = get_tensor(interpreter, index);
    if (tensor && name == TfLiteTensorName(tensor)) {
      return {tensor, index};
    }
  }

  std::string tensors;
  tensors.reserve(0x100);
  for (int index = 0; index < count; ++index) {
    auto tensor = get_tensor(interpreter, index);
    (tensors += ' ') += TfLiteTensorName(tensor);
  }
  throw std::runtime_error("No tensor for given " + kind + " with name = '" + name + "', expected one of:" + tensors);
}

} // namespace

namespace giga_edge {

TensorPtr TfLiteInterpreter::Input(const std::string& name) const {
  auto raw_tensor = GetTensor(
    name,
    "input",
    interpreter_.get(),
    TfLiteInterpreterGetInputTensor,
    TfLiteInterpreterGetInputTensorCount(interpreter_.get())
  );
  return std::make_shared<giga_edge::TfLiteTensor>(raw_tensor.first, raw_tensor.second);
}

TensorPtr TfLiteInterpreter::Output(const std::string& name) const {
  auto raw_tensor = GetTensor(
    name,
    "output",
    interpreter_.get(),
    TfLiteInterpreterGetOutputTensor,
    TfLiteInterpreterGetOutputTensorCount(interpreter_.get())
  );
  return std::make_shared<giga_edge::TfLiteTensor>(raw_tensor.first, raw_tensor.second);
}

std::vector<std::string> TfLiteInterpreter::InputNames() const {
  std::vector<std::string> result;
  for (int index = 0, count = TfLiteInterpreterGetInputTensorCount(interpreter_.get()); index < count; ++index) {
    result.emplace_back(TfLiteTensorName(TfLiteInterpreterGetInputTensor(interpreter_.get(), index)));
  }
  return result;
}

std::vector<std::string> TfLiteInterpreter::OutputNames() const {
  std::vector<std::string> result;
  for (int index = 0, count = TfLiteInterpreterGetOutputTensorCount(interpreter_.get()); index < count; ++index) {
    result.emplace_back(TfLiteTensorName(TfLiteInterpreterGetOutputTensor(interpreter_.get(), index)));
  }
  return result;
}

TensorPtr TfLiteInterpreter::Input(int id) const {
  return std::make_shared<TfLiteTensor>(TfLiteInterpreterGetInputTensor(interpreter_.get(), id), id);
}

TensorPtr TfLiteInterpreter::Output(int id) const {
  return std::make_shared<TfLiteTensor>(TfLiteInterpreterGetOutputTensor(interpreter_.get(), id), id);
}

int TfLiteInterpreter::InputsCount() const {
  return TfLiteInterpreterGetInputTensorCount(interpreter_.get());
}

int TfLiteInterpreter::OutputsCount() const {
  return TfLiteInterpreterGetOutputTensorCount(interpreter_.get());
}

void TfLiteInterpreter::ResizeInput(int id, const int* dims, int dims_count) {
  if (TfLiteInterpreterResizeInputTensor(interpreter_.get(), id, dims, dims_count) != kTfLiteOk) {
    throw std::runtime_error("Failed to resize input tensor");
  }
}

void TfLiteInterpreter::ResizeInput(const std::string& name, const int* dims, int dims_count) {
  auto raw_tensor = GetTensor(
    name,
    "input",
    interpreter_.get(),
    TfLiteInterpreterGetInputTensor,
    TfLiteInterpreterGetInputTensorCount(interpreter_.get())
  );
  ResizeInput(raw_tensor.second, dims, dims_count);
}

void TfLiteInterpreter::ReallocateTensors() {
  if (TfLiteInterpreterAllocateTensors(interpreter_.get()) != kTfLiteOk) {
    throw std::runtime_error("Failed to reallocate tensors");
  }
}

TfLiteInterpreter::TfLiteInterpreter(
  const TfLiteInterpreterParams& params, const std::string& path, const IDataReaderPtr& data_reader
)
  : params_(params)
  , model_(nullptr, TfLiteModelDelete)
  , xnnpack_delegate_(nullptr, nullptr)
  , interpreter_(nullptr, TfLiteInterpreterDelete) {
  if (!data_reader) {
    model_.reset(TfLiteModelCreateFromFile(path.c_str()));
    if (!model_) {
      throw std::runtime_error("Unable to create tflite model from file system path " + path);
    }
  } else {
    model_data_ = EnsureNotNull(data_reader->ReadPersistentData(path), path);
    auto [data, size] = model_data_->Get();
    model_.reset(TfLiteModelCreate(data, size));
    if (!model_) {
      throw std::runtime_error("Unable to create tflite model from data reader path " + path);
    }
  }

  PrepareInterpreter();
}

void TfLiteInterpreter::Invoke() {
  if (TfLiteInterpreterInvoke(interpreter_.get()) != kTfLiteOk) {
    throw std::runtime_error("Unable to invoke prediction");
  }
}

void TfLiteInterpreter::PrepareInterpreter() {
  std::unique_ptr<TfLiteInterpreterOptions, decltype(TfLiteInterpreterOptionsDelete)*> options(
    TfLiteInterpreterOptionsCreate(), TfLiteInterpreterOptionsDelete
  );
  TfLiteInterpreterOptionsSetNumThreads(options.get(), 1);
  RegisterUnpack5Bit(options.get());

  if (params_.use_xnnpack) {
#ifdef GIGA_EDGE_TFLITE_XNNPACK
    auto xnnpack_options = TfLiteXNNPackDelegateOptionsDefault();
    xnnpack_options.num_threads = 1;
    xnnpack_delegate_ = {TfLiteXNNPackDelegateCreate(&xnnpack_options), TfLiteXNNPackDelegateDelete};
    if (!xnnpack_delegate_) {
      throw std::runtime_error("Unable to create XNNPACK delegate");
    }
    TfLiteInterpreterOptionsAddDelegate(options.get(), xnnpack_delegate_.get());
#else
    throw std::runtime_error(
      "tflite use_xnnpack: tflite is built without XNNPACK (build_giga_edge.sh --tflite-xnnpack)"
    );
#endif
  }

  interpreter_.reset(TfLiteInterpreterCreate(model_.get(), options.get()));
  if (!interpreter_) {
    throw std::runtime_error("Unable to create interpreter with given model");
  }

  if (TfLiteInterpreterAllocateTensors(interpreter_.get()) != kTfLiteOk) {
    throw std::runtime_error("Unable to allocate tensors");
  }
}

} // namespace giga_edge
