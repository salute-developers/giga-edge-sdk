#import "giga_edge/modules/model/backends/coreml/interpreter.h"
#import "giga_edge/modules/model/backends/coreml/tensor.h"

#import <CoreML/CoreML.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if !__has_feature(objc_arc)
#error This file must be compiled with ARC
#endif

namespace giga_edge {

namespace {

std::string Describe(NSError* error) {
  return error ? std::string(error.localizedDescription.UTF8String) : std::string("unknown error");
}

Tensor::DataType TensorType(MLMultiArrayDataType type, const std::string& name) {
  switch (type) {
    case MLMultiArrayDataTypeFloat32:
    case MLMultiArrayDataTypeFloat16:
    case MLMultiArrayDataTypeDouble:
      return Tensor::DataType::FLOAT32;
    case MLMultiArrayDataTypeInt32:
      return Tensor::DataType::INT32;
    default:
      throw std::runtime_error("CoreML: unsupported multi-array type of '" + name + "'");
  }
}

float HalfToFloat(uint16_t half) {
  const uint32_t sign = (half & 0x8000u) << 16;
  uint32_t exponent = (half >> 10) & 0x1fu;
  uint32_t mantissa = half & 0x3ffu;
  uint32_t bits;
  if (exponent == 0x1f) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else if (exponent != 0) {
    bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
  } else if (mantissa == 0) {
    bits = sign;
  } else {
    exponent = 113;
    while ((mantissa & 0x400u) == 0) {
      mantissa <<= 1;
      --exponent;
    }
    bits = sign | (exponent << 23) | ((mantissa & 0x3ffu) << 13);
  }
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint16_t FloatToHalf(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
  const uint32_t exponent = (bits >> 23) & 0xffu;
  uint32_t mantissa = bits & 0x7fffffu;
  if (exponent == 0xff) {
    return sign | 0x7c00u | (mantissa ? 0x200u : 0u);
  }
  const int half_exponent = static_cast<int>(exponent) - 127 + 15;
  if (half_exponent >= 0x1f) {
    return sign | 0x7c00u;
  }
  if (half_exponent <= 0) {
    if (half_exponent < -10) {
      return sign;
    }
    mantissa |= 0x800000u;
    const uint32_t shift = static_cast<uint32_t>(14 - half_exponent);
    uint32_t half_mantissa = mantissa >> shift;
    const uint32_t rest = mantissa & ((1u << shift) - 1);
    const uint32_t halfway = 1u << (shift - 1);
    if (rest > halfway || (rest == halfway && (half_mantissa & 1u))) {
      ++half_mantissa;
    }
    return sign | static_cast<uint16_t>(half_mantissa);
  }
  uint16_t half = sign | static_cast<uint16_t>(half_exponent << 10) | static_cast<uint16_t>(mantissa >> 13);
  const uint32_t rest = mantissa & 0x1fffu;
  if (rest > 0x1000u || (rest == 0x1000u && (half & 1u))) {
    ++half; // carrying into the exponent is the correct rounding
  }
  return half;
}

template<class Visit>
void ForEachElement(MLMultiArray* array, Visit&& visit) {
  const NSUInteger dims = array.shape.count;
  std::vector<NSInteger> shape(dims), strides(dims), index(dims, 0);
  NSInteger total = 1;
  for (NSUInteger d = 0; d < dims; ++d) {
    shape[d] = array.shape[d].integerValue;
    strides[d] = array.strides[d].integerValue;
    total *= shape[d];
  }
  if (total == 0) {
    return;
  }
  const NSInteger inner = dims ? shape[dims - 1] : 1;
  const NSInteger inner_stride = dims ? strides[dims - 1] : 1;
  for (NSInteger flat = 0; flat < total; flat += inner) {
    NSInteger offset = 0;
    for (NSUInteger d = 0; d + 1 < dims; ++d) {
      offset += index[d] * strides[d];
    }
    for (NSInteger i = 0; i < inner; ++i) {
      visit(static_cast<size_t>(flat + i), offset + i * inner_stride);
    }
    for (NSInteger d = static_cast<NSInteger>(dims) - 2; d >= 0; --d) {
      if (++index[d] < shape[d]) {
        break;
      }
      index[d] = 0;
    }
  }
}

void CopyToArray(CoreMLTensor& tensor, MLMultiArray* array) {
  void* base = array.dataPointer;
  switch (array.dataType) {
    case MLMultiArrayDataTypeFloat32: {
      const float* src = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) { static_cast<float*>(base)[at] = src[i]; });
      break;
    }
    case MLMultiArrayDataTypeFloat16: {
      const float* src = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) { static_cast<uint16_t*>(base)[at] = FloatToHalf(src[i]); });
      break;
    }
    case MLMultiArrayDataTypeDouble: {
      const float* src = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) { static_cast<double*>(base)[at] = src[i]; });
      break;
    }
    case MLMultiArrayDataTypeInt32: {
      const int* src = tensor.IntData();
      ForEachElement(array, [&](size_t i, NSInteger at) { static_cast<int32_t*>(base)[at] = src[i]; });
      break;
    }
    default:
      throw std::runtime_error("CoreML: unsupported input type of '" + std::string(tensor.GetName()) + "'");
  }
}

void CopyFromArray(MLMultiArray* array, CoreMLTensor& tensor) {
  std::vector<int> shape;
  for (NSNumber* dim in array.shape) {
    shape.push_back(dim.intValue);
  }
  tensor.Reshape(std::move(shape));
  const void* base = array.dataPointer;
  switch (array.dataType) {
    case MLMultiArrayDataTypeFloat32: {
      float* dst = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) { dst[i] = static_cast<const float*>(base)[at]; });
      break;
    }
    case MLMultiArrayDataTypeFloat16: {
      float* dst = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) { dst[i] = HalfToFloat(static_cast<const uint16_t*>(base)[at]); });
      break;
    }
    case MLMultiArrayDataTypeDouble: {
      float* dst = tensor.FloatData();
      ForEachElement(array, [&](size_t i, NSInteger at) {
        dst[i] = static_cast<float>(static_cast<const double*>(base)[at]);
      });
      break;
    }
    case MLMultiArrayDataTypeInt32: {
      int* dst = tensor.IntData();
      ForEachElement(array, [&](size_t i, NSInteger at) { dst[i] = static_cast<const int32_t*>(base)[at]; });
      break;
    }
    default:
      throw std::runtime_error("CoreML: unsupported output type of '" + std::string(tensor.GetName()) + "'");
  }
}

MLComputeUnits ComputeUnits(CoreMLComputeUnits units) {
  switch (units) {
    case CoreMLComputeUnits::CPU_ONLY:
      return MLComputeUnitsCPUOnly;
    case CoreMLComputeUnits::CPU_AND_GPU:
      return MLComputeUnitsCPUAndGPU;
    case CoreMLComputeUnits::ALL:
      return MLComputeUnitsAll;
    case CoreMLComputeUnits::CPU_AND_NEURAL_ENGINE:
      if (@available(macOS 13.0, iOS 16.0, *)) {
        return MLComputeUnitsCPUAndNeuralEngine;
      }
      return MLComputeUnitsAll;
  }
  return MLComputeUnitsAll;
}

class ModelFiles {
public:
  ModelFiles(const std::string& path, const IDataReaderPtr& data_reader) {
    const auto prefix = data_reader ? data_reader->GetPathPrefix() : std::nullopt;
    if (!data_reader || prefix) {
      path_ = data_reader ? *prefix + path : path;
      if (!std::filesystem::is_directory(path_)) {
        throw std::runtime_error("CoreML model '" + path_ + "' is not a compiled model directory (.mlmodelc)");
      }
      return;
    }
    // CoreML loads only from disk: unpack into a private temp dir for the load.
    std::random_device random;
    temporary_ = std::filesystem::temp_directory_path()
      / ("giga_edge_coreml_" + std::to_string(random()) + std::to_string(random()));
    std::filesystem::create_directory(temporary_);
    std::filesystem::permissions(temporary_, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    path_ = (temporary_ / std::filesystem::path(path).filename()).string();
    try {
      Copy(*data_reader, path, path_);
    } catch (...) {
      Remove();
      throw;
    }
  }

  ~ModelFiles() {
    Remove();
  }

  const std::string& Path() const {
    return path_;
  }

  void Remove() {
    if (!temporary_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(temporary_, ignored);
      temporary_.clear();
    }
  }

private:
  static void Copy(IDataReader& reader, const std::filesystem::path& from, const std::filesystem::path& to) {
    if (!reader.IsDirectory(from)) {
      throw std::runtime_error("CoreML model '" + from.string() + "' is not a compiled model directory (.mlmodelc)");
    }
    std::filesystem::create_directories(to);
    for (const auto& child : reader.ListDirectories(from)) {
      Copy(reader, child, to / child.filename());
    }
    for (const auto& child : reader.ListFiles(from)) {
      // One file at a time to keep the peak low.
      const auto span = reader.ReadPersistentData(child.string());
      const auto data = span->Get();
      std::ofstream file(to / child.filename(), std::ios::binary);
      file.write(data.first, static_cast<std::streamsize>(data.second));
      if (!file) {
        throw std::runtime_error("CoreML: cannot unpack '" + child.string() + "'");
      }

    }
  }

  std::string path_;
  std::filesystem::path temporary_;
};

} // namespace

struct CoreMLInterpreter::Impl {
  MLModel* model = nil;
  std::vector<std::shared_ptr<CoreMLTensor>> inputs;
  std::vector<std::shared_ptr<CoreMLTensor>> outputs;
  std::vector<MLMultiArray*> input_arrays;
  std::unordered_map<std::string, int> input_index;
  std::unordered_map<std::string, int> output_index;
};

CoreMLInterpreter::CoreMLInterpreter(
  const CoreMLInterpreterParams& params, const std::string& path, const IDataReaderPtr& data_reader
)
  : impl_(std::make_unique<Impl>()) {
  ModelFiles files(path, data_reader);
  const auto& model_path = files.Path();
  const auto started = std::chrono::steady_clock::now();
  @autoreleasepool {
    MLModelConfiguration* configuration = [[MLModelConfiguration alloc] init];
    configuration.computeUnits = ComputeUnits(params.compute_units);
    NSError* error = nil;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:model_path.c_str()] isDirectory:YES];
    impl_->model = [MLModel modelWithContentsOfURL:url configuration:configuration error:&error];
    if (!impl_->model) {
      throw std::runtime_error("CoreML: cannot load '" + model_path + "': " + Describe(error));
    }

    MLModelDescription* description = impl_->model.modelDescription;
    NSArray<NSString*>* input_names =
      [description.inputDescriptionsByName.allKeys sortedArrayUsingSelector:@selector(compare:)];
    for (NSString* name in input_names) {
      MLFeatureDescription* feature = description.inputDescriptionsByName[name];
      const std::string input_name(name.UTF8String);
      if (feature.type != MLFeatureTypeMultiArray || !feature.multiArrayConstraint.shape.count) {
        throw std::runtime_error("CoreML: input '" + input_name + "' is not a fixed-shape multi-array");
      }
      MLMultiArrayConstraint* constraint = feature.multiArrayConstraint;
      std::vector<int> shape;
      for (NSNumber* dim in constraint.shape) {
        shape.push_back(dim.intValue);
      }
      MLMultiArray* array = [[MLMultiArray alloc] initWithShape:constraint.shape
                                                       dataType:constraint.dataType
                                                          error:&error];
      if (!array) {
        throw std::runtime_error("CoreML: cannot allocate input '" + input_name + "': " + Describe(error));
      }
      const int index = static_cast<int>(impl_->inputs.size());
      impl_->input_index[input_name] = index;
      impl_->inputs.push_back(std::make_shared<CoreMLTensor>(
        input_name, index, TensorType(constraint.dataType, input_name), std::move(shape)
      ));
      impl_->input_arrays.push_back(array);
    }

    NSArray<NSString*>* output_names =
      [description.outputDescriptionsByName.allKeys sortedArrayUsingSelector:@selector(compare:)];
    for (NSString* name in output_names) {
      MLFeatureDescription* feature = description.outputDescriptionsByName[name];
      const std::string output_name(name.UTF8String);
      if (feature.type != MLFeatureTypeMultiArray) {
        throw std::runtime_error("CoreML: output '" + output_name + "' is not a multi-array");
      }
      std::vector<int> shape;
      for (NSNumber* dim in feature.multiArrayConstraint.shape) {
        shape.push_back(dim.intValue);
      }
      const int index = static_cast<int>(impl_->outputs.size());
      impl_->output_index[output_name] = index;
      impl_->outputs.push_back(std::make_shared<CoreMLTensor>(
        output_name, index, TensorType(feature.multiArrayConstraint.dataType, output_name), std::move(shape)
      ));
    }
  }
  const auto loaded = std::chrono::steady_clock::now();

  if (params.warmup) {
    Invoke();
    files.Remove();
    const auto warmed = std::chrono::steady_clock::now();
    std::cerr << "[coreml] " << std::filesystem::path(model_path).filename().string() << " loaded in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(loaded - started).count() << " ms, warmed up in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(warmed - loaded).count() << " ms" << std::endl;
  }
}

CoreMLInterpreter::~CoreMLInterpreter() = default;

void CoreMLInterpreter::Invoke() {
  // Graph threads have no autorelease pool: CoreML outputs would pile up.
  @autoreleasepool {
    NSMutableDictionary<NSString*, id>* features = [NSMutableDictionary dictionary];
    for (size_t i = 0; i < impl_->inputs.size(); ++i) {
      CopyToArray(*impl_->inputs[i], impl_->input_arrays[i]);
      NSString* name = [NSString stringWithUTF8String:std::string(impl_->inputs[i]->GetName()).c_str()];
      features[name] = impl_->input_arrays[i];
    }
    NSError* error = nil;
    MLDictionaryFeatureProvider* provider = [[MLDictionaryFeatureProvider alloc] initWithDictionary:features
                                                                                               error:&error];
    if (!provider) {
      throw std::runtime_error("CoreML: cannot pass the inputs: " + Describe(error));
    }
    id<MLFeatureProvider> result = [impl_->model predictionFromFeatures:provider error:&error];
    if (!result) {
      throw std::runtime_error("CoreML: prediction failed: " + Describe(error));
    }
    for (auto& output : impl_->outputs) {
      NSString* name = [NSString stringWithUTF8String:std::string(output->GetName()).c_str()];
      MLMultiArray* array = [result featureValueForName:name].multiArrayValue;
      if (!array) {
        throw std::runtime_error("CoreML: prediction has no output '" + std::string(output->GetName()) + "'");
      }
      CopyFromArray(array, *output);
    }
  }
}

TensorPtr CoreMLInterpreter::Input(const std::string& name) const {
  const auto it = impl_->input_index.find(name);
  if (it == impl_->input_index.end()) {
    throw std::runtime_error("CoreML: no input '" + name + "'");
  }
  return impl_->inputs[it->second];
}

TensorPtr CoreMLInterpreter::Output(const std::string& name) const {
  const auto it = impl_->output_index.find(name);
  if (it == impl_->output_index.end()) {
    throw std::runtime_error("CoreML: no output '" + name + "'");
  }
  return impl_->outputs[it->second];
}

TensorPtr CoreMLInterpreter::Input(int id) const {
  return impl_->inputs.at(id);
}

TensorPtr CoreMLInterpreter::Output(int id) const {
  return impl_->outputs.at(id);
}

std::vector<std::string> CoreMLInterpreter::InputNames() const {
  std::vector<std::string> names;
  for (const auto& input : impl_->inputs) {
    names.emplace_back(input->GetName());
  }
  return names;
}

std::vector<std::string> CoreMLInterpreter::OutputNames() const {
  std::vector<std::string> names;
  for (const auto& output : impl_->outputs) {
    names.emplace_back(output->GetName());
  }
  return names;
}

int CoreMLInterpreter::InputsCount() const {
  return static_cast<int>(impl_->inputs.size());
}

int CoreMLInterpreter::OutputsCount() const {
  return static_cast<int>(impl_->outputs.size());
}

void CoreMLInterpreter::ResizeInput(int id, const int* dims, int dims_count) {
  const auto& tensor = impl_->inputs.at(id);
  if (tensor->GetShape() != std::vector<int>(dims, dims + dims_count)) {
    throw std::runtime_error(
      "CoreML input '" + std::string(tensor->GetName()) + "' has a fixed shape; pad the data in the graph instead"
    );
  }
}

void CoreMLInterpreter::ResizeInput(const std::string& name, const int* dims, int dims_count) {
  ResizeInput(std::dynamic_pointer_cast<CoreMLTensor>(Input(name))->GetIndex(), dims, dims_count);
}

void CoreMLInterpreter::ReallocateTensors() {}

} // namespace giga_edge
