# Everything that is open: all the modules of the open-source tree and all its inference backends.
set(MODULES
  assistant_core audio audio_io common enhance events features fst function_calling llm mcp mcp_http model
  personalization smart_home_ha spotter synthesis_text tts web web_transport_curl
)
set(TFLITE_ENABLED ON)
set(ONNXRUNTIME_ENABLED ON)
set(USE_VITS_BACKEND ON)  # tts: the neural synthesis, on ONNX Runtime
set(LLAMA_CPP_ENABLED ON)
set(NCNN_ENABLED ON)
