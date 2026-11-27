#include "giga_edge/modules/llm/detail/llm_local_debug.h"

#include <cstdlib>

// Capturing stderr through dup is POSIX only.
#if defined(LLAMA_CPP_ENABLED) && !defined(_WIN32)
#define GIGA_EDGE_TEST_LLAMA_LOG_ROUTING 1
#include "llama.h"

#include <catch.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>
#endif

namespace {

// The model loading dump of llama.cpp pushes the CI log to its limit: tests log only errors and warnings, and
// everything with GIGA_EDGE_TEST_LLAMA_LOG=1.
const bool kLlamaLogConfigured = [] {
  if (std::getenv("GIGA_EDGE_TEST_LLAMA_LOG") == nullptr) {
    giga_edge::ConfigureLlamaLogging();
  }
  return true;
}();

} // namespace

#ifdef GIGA_EDGE_TEST_LLAMA_LOG_ROUTING
namespace {

// What the llama.cpp log callback writes to stderr.
std::string Routed(ggml_log_callback callback, void* user_data, ggml_log_level level, const char* text) {
  // An unnamed file: parallel runs do not interfere with each other.
  std::FILE* captured = std::tmpfile();
  REQUIRE(captured != nullptr);
  const int file = fileno(captured);
  std::fflush(stderr);
  const int saved = dup(fileno(stderr));
  REQUIRE(saved >= 0);
  REQUIRE(file >= 0);
  dup2(file, fileno(stderr));
  callback(level, text, user_data);
  std::fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  REQUIRE(std::fseek(captured, 0, SEEK_SET) == 0);
  std::string out;
  for (int c = std::fgetc(captured); c != EOF; c = std::fgetc(captured)) {
    out.push_back(static_cast<char>(c));
  }
  std::fclose(captured);
  return out;
}

} // namespace

TEST_CASE("llama.cpp errors and warnings reach stderr, the detailed log does not", "[llm][log]") {
  ggml_log_callback previous = nullptr;
  void* previous_data = nullptr;
  llama_log_get(&previous, &previous_data);
  giga_edge::ConfigureLlamaLogging();
  ggml_log_callback callback = nullptr;
  void* user_data = nullptr;
  llama_log_get(&callback, &user_data);
  REQUIRE(callback != nullptr);

  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_ERROR, "error\n") == "error\n");
  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_WARN, "warning\n") == "warning\n");
  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_CONT, "continued\n") == "continued\n");
  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_INFO, "info\n").empty());
  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_CONT, "continued\n").empty());
  REQUIRE(Routed(callback, user_data, GGML_LOG_LEVEL_DEBUG, "debug\n").empty());

  llama_log_set(previous, previous_data);
}
#endif
