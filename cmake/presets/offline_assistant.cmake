# What the offline voice assistant of example/offline_assistant needs: hotword or text -> speech recognition and end
# of utterance -> LLM with tool calls -> Home Assistant over MCP -> speech synthesis of the reply.
set(MODULES
  assistant_core audio_io common events features function_calling llm mcp mcp_http model personalization
  smart_home_ha spotter synthesis_text tts web web_transport_curl
)
set(TFLITE_ENABLED ON)       # hotword: tflite
set(ONNXRUNTIME_ENABLED ON)  # asr / eou / tts: onnx
set(USE_VITS_BACKEND ON)     # tts: the synthesis of the replies
set(LLAMA_CPP_ENABLED ON)    # function_calling: gguf
