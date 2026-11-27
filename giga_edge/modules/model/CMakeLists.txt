cmake_minimum_required(VERSION 3.14)

# Module options: the inference backends.
option(TFLITE_ENABLED "Compile with TfLite." ON)
option(TFLITE_XNNPACK "tflite is built with XNNPACK (build_giga_edge.sh --tflite-xnnpack)." OFF)
option(ONNXRUNTIME_ENABLED "Compile with ONNXRuntime." OFF)
option(NCNN_ENABLED "Compile with ncnn." OFF)
option(COREML_ENABLED "Compile with CoreML." OFF)

giga_edge_module(model
  SRCS
    src/interpreter/interpreter.cpp
    src/interpreter/tensor.cpp
    src/models/model.cpp
    src/models/crnn/crnn.cpp
    src/models/crnn/window_crnn.cpp
    src/models/crnn/base_crnn_model.cpp
    src/models/crnn/params.cpp
    src/nodes/model/crnn_model.cpp
    src/nodes/model/crnn_model_params.cpp
    src/nodes/model/rnnt_model.cpp
    src/nodes/model/rnnt_model_params.cpp
    src/nodes/model/model.cpp
    src/nodes/model/model_params.cpp
    src/nodes/model/inference_strategies/inference_strategy.cpp
    src/nodes/model/inference_strategies/inference_strategy_params.cpp
    src/nodes/model/inference_strategies/inference_strategy_factory.cpp
    src/nodes/model/inference_strategies/queue_strategy.cpp
    src/nodes/model/inference_strategies/sync_inference_strategy.cpp
    src/nodes/model/inference_strategies/skip_if_inferring_strategy.cpp
    src/nodes/model/inference_strategies/wait_till_inferring_strategy.cpp
    src/nodes/model/inference_strategies/wait_number_of_chunks_strategy.cpp
    src/nodes/asr/predictions_to_text.cpp
    src/nodes/asr/predictions_to_text_params.cpp
    src/config_parser/common/interpreter.cpp
    src/config_parser/common/model.cpp
    src/config_parser/yaml/interpreter.cpp
    src/config_parser/yaml/model.cpp
  DEPS
    node_lib
    config_parser_lib
    data_reader_lib
    enum_lib
    environment_lib
    common_lib
    version_lib
    token_dictionary_lib
    packing_lib
)

# The inference backends are configured only when the module is selected for the build. The definitions are PUBLIC:
# the layout of InterpreterParams (internal/.../interpreter/params.h) depends on them, so all dependent TUs must
# see the same set.

module_enabled(model _model_enabled)
if(_model_enabled)

if(APPLE AND COREML_ENABLED)
  message("${Blue}-- Configuring with CoreML version. Please use CoreML models.${Clear}")
  target_compile_definitions(model_module PUBLIC COREML_ENABLED=1)
  target_sources(model_module PRIVATE
    src/backends/coreml/interpreter.mm
    src/backends/coreml/tensor.cpp)
  target_compile_options(model_module PRIVATE "-fobjc-arc")
  find_library(COREML_LIBRARIES CoreML)
  target_link_libraries(model_module PUBLIC ${COREML_LIBRARIES})
endif()


if(TFLITE_ENABLED)
  find_package(Eigen3 CONFIG REQUIRED)
  find_package(tensorflow-lite CONFIG REQUIRED)

  target_compile_definitions(model_module PUBLIC TFLITE_ENABLED=1)
  target_sources(model_module PRIVATE
    src/backends/tflite/interpreter.cpp
    src/backends/tflite/tensor.cpp
    src/backends/tflite/op_unpack_5bit.cpp)
  target_link_libraries(model_module PUBLIC tensorflow-lite::tensorflow-lite)
  # tflite is built with XNNPACK (build_giga_edge.sh --tflite-xnnpack): the delegate is used per the model option
  # tflite.use_xnnpack.
  if(TARGET tensorflow-lite::xnnpack-delegate)
    target_compile_definitions(model_module PUBLIC GIGA_EDGE_TFLITE_XNNPACK=1)
  elseif(TFLITE_XNNPACK)
    message(FATAL_ERROR "TFLITE_XNNPACK: the installed tflite is built without XNNPACK, "
                        "rebuild the dependencies with --tflite-xnnpack")
  endif()
endif()


if(ONNXRUNTIME_ENABLED)
  message(STATUS "ONNX Runtime is enabled. Checking platform compatibility...")
  find_package(Eigen3 CONFIG REQUIRED)
  find_package(boost_mp11 CONFIG REQUIRED)
  find_package(Protobuf CONFIG REQUIRED)
  find_package(onnxruntime CONFIG REQUIRED)
  message(STATUS "ONNX Runtime configuration successful.")
  # An imported target is visible only in this directory, but the siblings need it too: make it global.
  set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_GLOBAL TRUE)

  target_compile_definitions(model_module PUBLIC ONNXRUNTIME_ENABLED=1)
  target_sources(model_module PRIVATE
    src/backends/onnxruntime/interpreter.cpp
    src/backends/onnxruntime/onnx_environment.cpp
    src/backends/onnxruntime/tensor.cpp)
  target_link_libraries(model_module PUBLIC onnxruntime::onnxruntime)
endif()

if(NCNN_ENABLED)
  message("${Blue}-- Configuring with ncnn version.")
  find_package(ncnn REQUIRED)
  target_compile_definitions(model_module PUBLIC NCNN_ENABLED=1)
  target_sources(model_module PRIVATE
    src/backends/ncnn/interpreter.cpp
    src/backends/ncnn/tensor.cpp)
  target_link_libraries(model_module PUBLIC ncnn)
endif()

endif() # _model_enabled
