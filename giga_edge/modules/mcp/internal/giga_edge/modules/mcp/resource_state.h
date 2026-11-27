#pragma once

#include "giga_edge/modules/mcp/client.h"

#include <json/json.h>

namespace giga_edge {
namespace mcp {

// The resource state that the service publishes after resources/read: uri, mimeType, text, and data (the parsed text
// if it is JSON).
Json::Value BuildResourceContentState(const ResourceContent& content);

} // namespace mcp
} // namespace giga_edge
