#pragma once

#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/lib/threadpool/threadpool.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace YAML { // NOLINT(readability-identifier-naming): external yaml-cpp namespace
class Node;
} // namespace YAML

namespace giga_edge {

struct ThreadPoolEnvironmentParams {
  int threads_count = 1;
  bool realtime_priority = false;
  int realtime_threads_count = 0;
  std::string thread_name_prefix;
};

struct EnvironmentParams {
  enum class Type { THREAD_POOL, EMPTY, EXTERNAL } type = Type::EMPTY;

  ThreadPoolEnvironmentParams thread_pool_params;

  std::string external_type;
  std::shared_ptr<void> external_config;
};

struct EmptyEnvironment : Environment {};

using EnvironmentsMapParams = std::map<std::string, EnvironmentParams>;

struct ThreadPoolEnvironment : Environment {
  std::shared_ptr<IThreadPool> thread_pool;

  explicit ThreadPoolEnvironment(const EnvironmentParams& params);
};

EnvironmentPtr MakeEnvironment(const EnvironmentParams& params);

struct ExternalEnvironmentHandlers {
  std::function<std::shared_ptr<void>(const YAML::Node&)> parse;
  std::function<EnvironmentPtr(const std::shared_ptr<void>&)> make;
};

bool RegisterExternalEnvironment(const std::string& type, ExternalEnvironmentHandlers handlers);
const ExternalEnvironmentHandlers* FindExternalEnvironment(const std::string& type);

} // namespace giga_edge

// Registers an environment type for `model_environments: {<name>: {type: <name>}}`.
#define REGISTER_ENVIRONMENT(name, parse_config, make_environment)                                                     \
  namespace giga_edge {                                                                                                \
  [[maybe_unused]] inline const bool kEnvironmentRegistered_##name = ::giga_edge::RegisterExternalEnvironment(         \
    #name, ::giga_edge::ExternalEnvironmentHandlers{parse_config, make_environment}                                    \
  );                                                                                                                   \
  }
