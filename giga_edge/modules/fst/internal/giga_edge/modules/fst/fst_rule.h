#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class IFstRule {
public:
  virtual std::string Apply(const std::string& input) const = 0;
  virtual std::vector<std::string> ApplyAll(const std::string& input, std::size_t max_outputs) const = 0;
  virtual ~IFstRule() = default;
};

using FstRulePtr = std::shared_ptr<IFstRule>;

class IDataReader;
using DataReaderPtr = std::shared_ptr<IDataReader>;

FstRulePtr ReadFstRule(IDataReader& data_reader, const std::string& path);
FstRulePtr ReadFstRule(const DataReaderPtr& data_reader, const std::string& path);

} // namespace giga_edge
