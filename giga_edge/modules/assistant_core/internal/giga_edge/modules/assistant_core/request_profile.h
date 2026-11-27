#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class IRequestProfile {
public:
  virtual ~IRequestProfile() = default;

  virtual std::string SystemPrompt(const std::vector<std::string>& instructions) const = 0;
  virtual std::string UserMessage(const std::vector<std::string>& instructions, const std::string& text) const = 0;
};

using RequestProfilePtr = std::shared_ptr<IRequestProfile>;
using RequestProfileFactory = std::function<RequestProfilePtr(const YAML::Node& params)>;

class RequestProfileRegistry {
public:
  static bool Register(const std::string& name, RequestProfileFactory factory);
  static RequestProfilePtr Create(const std::string& name, const YAML::Node& params);
  static bool Has(const std::string& name);
  static std::vector<std::string> Names();
};

#define REGISTER_REQUEST_PROFILE(profile_name, factory)                                                                \
  inline const bool profile_name##_request_profile_registered = RequestProfileRegistry::Register(                      \
    #profile_name, factory                                                                                             \
  );

} // namespace giga_edge
