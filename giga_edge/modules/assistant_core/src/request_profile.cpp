#include "giga_edge/modules/assistant_core/request_profile.h"

#include <map>
#include <stdexcept>

namespace giga_edge {
namespace {

std::map<std::string, RequestProfileFactory>& Factories() {
  static std::map<std::string, RequestProfileFactory> factories;
  return factories;
}

} // namespace

bool RequestProfileRegistry::Register(const std::string& name, RequestProfileFactory factory) {
  if (!Factories().emplace(name, std::move(factory)).second) {
    throw std::runtime_error("Request profile is already registered: " + name);
  }
  return true;
}

RequestProfilePtr RequestProfileRegistry::Create(const std::string& name, const YAML::Node& params) {
  const auto it = Factories().find(name);
  if (it == Factories().end()) {
    throw std::runtime_error("Unknown request profile: " + name);
  }
  return it->second(params);
}

bool RequestProfileRegistry::Has(const std::string& name) {
  return Factories().count(name) > 0;
}

std::vector<std::string> RequestProfileRegistry::Names() {
  std::vector<std::string> names;
  for (const auto& [name, _] : Factories()) {
    names.push_back(name);
  }
  return names;
}

} // namespace giga_edge
