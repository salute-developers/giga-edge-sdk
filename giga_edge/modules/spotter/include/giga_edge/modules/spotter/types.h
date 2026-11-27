#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace giga_edge {

enum class HotwordGroup {
  NONE = 0,
  MAIN = 1,
  ASSISTANT = 2,
};

class IClassMetaInfo {
public:
  virtual ~IClassMetaInfo() = default;

  virtual float GetThreshold() const = 0;
  virtual float GetSubThreshold() const = 0;
  virtual float GetOvThreshold() const = 0;
  virtual std::unordered_map<std::string, std::string> GetAsrVariables() const = 0;

  virtual std::string GetHotword() const = 0;
  virtual std::string GetRuHotword() const = 0;
  virtual HotwordGroup GetHotwordGroup() const = 0;

  virtual int GetHotwordLengthMs() const = 0;
};

using IClassMetaInfoPtr = std::unique_ptr<IClassMetaInfo>;

enum class ActivationMode { MAIN, MAIN_AND_ACTIVE, ALL };

struct SpotterState {
  ActivationMode activation_mode;
  std::string active_name;
  std::string synthesis_text;
};

} // namespace giga_edge
