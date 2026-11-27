cmake_minimum_required(VERSION 3.14)

option(LLAMA_CPP_ENABLED "Enable the llama.cpp backend for LLM" OFF)

giga_edge_module(llm
  SRCS
    src/detail/llm_local_debug.cpp
    src/interpreter/interpreter_factory.cpp
    src/kv_transcript.cpp
    src/prompt_planner.cpp
    src/special_text_escape.cpp
    src/nodes/llm_generate/node.cpp
    src/nodes/llm_generate/params.cpp
    src/nodes/llm_passthrough/node.cpp
    src/parsers/xml_tool_call_parser.cpp
  DEPS
    assistant_core_module
    data_reader_lib
    node_lib
    config_parser_lib
    environment_lib
    threadpool_lib
)

module_enabled(llm _llm_enabled)
if(_llm_enabled AND LLAMA_CPP_ENABLED)
  message(STATUS "--- Backend 'llama.cpp' enabled (LLAMA_CPP_ENABLED=ON)")

  find_package(Threads REQUIRED)
  find_package(llama REQUIRED)

  target_compile_definitions(llm_module PUBLIC LLAMA_CPP_ENABLED=1)
  target_sources(llm_module PRIVATE
    src/interpreter/engine/llama.cpp/engine.cpp
    src/interpreter/engine/llama.cpp/prompt_head.cpp
  )
  target_link_libraries(llm_module PUBLIC llama Threads::Threads)

  if(GGML_CUDA)
    find_package(CUDAToolkit REQUIRED)

    target_link_libraries(llm_module PUBLIC
      CUDA::cudart
      CUDA::cuda_driver
      CUDA::cublas
      CUDA::cublasLt
    )

    if(GGML_CUDA_NCCL)
      find_library(NCCL_LIBRARY NAMES nccl)
      if(NOT NCCL_LIBRARY)
        message(FATAL_ERROR "GGML_CUDA_NCCL is enabled, but libnccl was not found")
      endif()

      target_link_libraries(llm_module PUBLIC ${NCCL_LIBRARY})
    endif()
  endif()
else()
  message(STATUS "--- Backend 'llama.cpp' disabled (LLAMA_CPP_ENABLED=OFF)")
endif()
