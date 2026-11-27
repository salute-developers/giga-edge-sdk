#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/user_query_from_text/node.h"
#include "giga_edge/modules/assistant_core/text.h"

namespace giga_edge {

template<>
UserQueryFromTextParams ParseConfig(const YAML::Node& config) {
  UserQueryFromTextParams result;
  result.request_id_prefix = config["request_id_prefix"].as<std::string>(result.request_id_prefix);
  return result;
}

UserQueryFromText::UserQueryFromText(const NodeBaseConfig& base_config, const UserQueryFromTextParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void UserQueryFromText::Reset() {
  next_request_id_ = 0;
}

void UserQueryFromText::ProcessOnce() {
  UserQuery query;
  query.text = Trim(GetText());
  if (query.text.empty()) {
    query.status = AssistantStatus::SKIPPED;
    PushUserQuery(std::move(query));
    return;
  }
  const auto request_id = GetRequestId();
  query.request_id = request_id.has_value() && !request_id.value().get().empty()
    ? request_id.value().get()
    : params_.request_id_prefix + std::to_string(next_request_id_++);
  PushUserQuery(std::move(query));
}

} // namespace giga_edge
