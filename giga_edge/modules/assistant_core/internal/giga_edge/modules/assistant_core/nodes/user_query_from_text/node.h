#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstdint>
#include <string>

namespace giga_edge {

struct UserQueryFromTextParams {
  std::string request_id_prefix = "request-";
};

template<>
UserQueryFromTextParams ParseConfig(const YAML::Node& config);

class UserQueryFromText : public NodeBase {
public:
  UserQueryFromText(const NodeBaseConfig& base_config, const UserQueryFromTextParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  REGISTER_INPUT(user_query_from_text, text, Text, std::string, "Recognized or typed user text; empty on idle ticks.");
  REGISTER_OPTIONAL_INPUT(
    user_query_from_text,
    request_id,
    RequestId,
    std::string,
    "Caller-provided request id; when absent or empty the node numbers requests with request_id_prefix."
  );
  REGISTER_OUTPUT(
    user_query_from_text, user_query, UserQuery, UserQuery, "UserQuery with a fresh request_id per non-empty text."
  );

  UserQueryFromTextParams params_;
  uint64_t next_request_id_ = 0;
};

REGISTER_NODE(user_query_from_text, UserQueryFromText, UserQueryFromTextParams, "Wraps plain text into a UserQuery.");

} // namespace giga_edge
