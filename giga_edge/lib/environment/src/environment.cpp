#include "giga_edge/lib/environment/environment.h"

#include <stdexcept>
#include <unordered_map>

namespace {

std::unordered_map<std::string, giga_edge::ExternalEnvironmentHandlers>& ExternalRegistry() {
  static std::unordered_map<std::string, giga_edge::ExternalEnvironmentHandlers> registry;
  return registry;
}

} // namespace

namespace giga_edge {

ThreadPoolEnvironment::ThreadPoolEnvironment(const EnvironmentParams& params)
  : thread_pool(
      std::make_shared<ThreadPool>(
        params.thread_pool_params.threads_count,
        params.thread_pool_params.realtime_priority,
        params.thread_pool_params.realtime_threads_count,
        params.thread_pool_params.thread_name_prefix
      )
    ) {}

bool RegisterExternalEnvironment(const std::string& type, ExternalEnvironmentHandlers handlers) {
  ExternalRegistry()[type] = std::move(handlers);
  return true;
}

const ExternalEnvironmentHandlers* FindExternalEnvironment(const std::string& type) {
  const auto it = ExternalRegistry().find(type);
  return it == ExternalRegistry().end() ? nullptr : &it->second;
}

EnvironmentPtr MakeEnvironment(const EnvironmentParams& params) {
  switch (params.type) {
    case EnvironmentParams::Type::THREAD_POOL:
      return std::make_shared<ThreadPoolEnvironment>(params);
    case EnvironmentParams::Type::EMPTY:
      return std::make_shared<EmptyEnvironment>();
    case EnvironmentParams::Type::EXTERNAL: {
      const auto* handlers = FindExternalEnvironment(params.external_type);
      if (handlers == nullptr || !handlers->make) {
        throw std::runtime_error("Environment type '" + params.external_type + "' is not registered in this build");
      }
      return handlers->make(params.external_config);
    }
  }

  throw std::invalid_argument("Invalid environment type");
}

} // namespace giga_edge
