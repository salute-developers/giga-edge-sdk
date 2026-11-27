#include "giga_edge/modules/llm/interpreter/interpreter.h"

#include <stdexcept> // IWYU pragma: keep

#ifdef LLAMA_CPP_ENABLED
#include "giga_edge/modules/llm/interpreter/engine/llama.cpp/engine.h"
#endif

namespace giga_edge {

std::unique_ptr<ILlmInterpreter> LlmInterpreterFactory::CreateInterpreter(BackendType type) {
  switch (type) {
    case BackendType::LLAMA_CPP:
#ifdef LLAMA_CPP_ENABLED
      return std::make_unique<LlamaCppEngine>();
#else
      throw std::runtime_error("Llama.cpp backend is not supported (LLAMA_CPP_ENABLED=OFF)");
#endif
    default:
      return nullptr;
  }
}

} // namespace giga_edge
