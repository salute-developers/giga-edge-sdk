#pragma once

#include "giga_edge/modules/model/interpreter/tensor.h"

#include "onnxruntime/onnxruntime_cxx_api.h"

#include <stdexcept>

namespace giga_edge {
namespace onnx_utils {

inline size_t GetElementSize(ONNXTensorElementDataType type) {
  switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return sizeof(float);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return sizeof(int32_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return sizeof(int64_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return sizeof(uint8_t);
    default:
      throw std::runtime_error("Unsupported ONNX tensor type for size calculation: " + std::to_string(type));
  }
}

inline Tensor::DataType ToTensorDataType(ONNXTensorElementDataType type) {
  switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return Tensor::DataType::FLOAT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return Tensor::DataType::INT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return Tensor::DataType::INT64;
    default:
      throw std::runtime_error("Unsupported ONNX tensor type " + std::to_string(type));
  }
}

} // namespace onnx_utils
} // namespace giga_edge
