#include "giga_edge/modules/model/backends/tflite/op_unpack_5bit.h"

#include "giga_edge/lib/packing/5bit.h"

#if GIGA_EDGE_EXPERIMENTAL_TFLITE_C_API
#include "tensorflow/lite/experimental/c/c_api_experimental.h"
#else
#include "tensorflow/lite/c/c_api_experimental.h"
#endif

namespace {

struct IOContext {
  const TfLiteTensor* input_tensor;
  TfLiteTensor* output_tensor;
  const std::uint8_t* input_data; // not validated

  TfLiteStatus ValidateAndInit(TfLiteContext* ctx, TfLiteNode* node) {
    TF_LITE_ENSURE(
      ctx,
      node->inputs && (node->inputs->size == 1) && (node->inputs->data[0] >= 0)
        && (node->inputs->data[0] < ctx->tensors_size)
    );
    TF_LITE_ENSURE(
      ctx,
      node->outputs && (node->outputs->size == 1) && (node->outputs->data[0] >= 0)
        && (node->outputs->data[0]) < ctx->tensors_size
    );
    input_tensor = &ctx->tensors[node->inputs->data[0]];
    output_tensor = &ctx->tensors[node->outputs->data[0]];
    input_data = static_cast<std::uint8_t*>(TfLiteTensorData(input_tensor));
    return kTfLiteOk;
  }
};

TfLiteStatus Unpack5BitPrepare(TfLiteContext* ctx, TfLiteNode* node) {
  IOContext io;
  if (auto s = io.ValidateAndInit(ctx, node); s != kTfLiteOk) {
    return s;
  }

  TF_LITE_ENSURE_TYPES_EQ(ctx, TfLiteTensorType(io.input_tensor), kTfLiteUInt8);
  auto output_type = TfLiteTensorType(io.output_tensor);
  TF_LITE_ENSURE(ctx, (output_type == kTfLiteInt8) || (output_type == kTfLiteUInt8));
  TF_LITE_ENSURE(
    ctx,
    (io.input_data != nullptr) && (TfLiteTensorByteSize(io.input_tensor) != 0)
      && (TfLiteTensorByteSize(io.output_tensor) != 0)
  );

  return kTfLiteOk;
}

TfLiteStatus Unpack5BitEval(TfLiteContext* ctx, TfLiteNode* node) {
  IOContext io;
  if (auto s = io.ValidateAndInit(ctx, node); s != kTfLiteOk) {
    return s;
  }
  std::uint8_t* output_data = static_cast<std::uint8_t*>(TfLiteTensorData(io.output_tensor));
  TF_LITE_ENSURE(ctx, (io.input_data != nullptr) && (output_data != nullptr));

  if (!giga_edge::packing::Unpack5BitData(
        io.input_data, TfLiteTensorByteSize(io.input_tensor), output_data, TfLiteTensorByteSize(io.output_tensor)
      )) {
    ctx->ReportError(ctx, "Failed to unpack 5-bit tensor");
    return kTfLiteError;
  }

  return kTfLiteOk;
}

const TfLiteRegistration* GetUnpack5BitRegistration() {
  static constexpr TfLiteRegistration r = {nullptr, nullptr, Unpack5BitPrepare, Unpack5BitEval};
  return &r;
}

} // namespace

namespace giga_edge {

void RegisterUnpack5Bit(TfLiteInterpreterOptions* options) {
  TfLiteInterpreterOptionsAddCustomOp(options, packing::unpack_5bit_custom_op_name, GetUnpack5BitRegistration(), 1, 1);
}

} // namespace giga_edge
