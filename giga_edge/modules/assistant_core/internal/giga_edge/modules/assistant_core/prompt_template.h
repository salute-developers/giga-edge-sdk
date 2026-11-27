#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace giga_edge {

// Prompt bytes outside the spans come from the template config. A foreign span is text from outside; a verified span
// may contain special tokens that lie entirely inside it (a special token emitted by the model, or an already verified
// assistant reply).
enum class TextOrigin { FOREIGN, VERIFIED };

struct TextSpan {
  size_t begin = 0;
  size_t end = 0;
  TextOrigin origin = TextOrigin::FOREIGN;
};

struct RoleFormat {
  std::string prefix;
  std::string suffix;
};

struct PromptTemplateParams {
  RoleFormat system;
  RoleFormat scene;
  RoleFormat user;
  RoleFormat assistant;
  RoleFormat tool;
  std::string generation_prefix;
  std::string system_instructions;
  bool render_empty_system = false;
  bool render_empty_scene = false;
  // Set when the scene is foreign text (a raw MCP catalog); by default its markup from the renderer is trusted.
  bool scene_literal = false;
  bool scene_in_user = false;
  bool render_tools = false;
  std::string tools_header;
  std::string tools_footer;
  std::string tool_call_prefix;
  std::string tool_call_suffix;
};

PromptTemplateParams ParsePromptTemplateParams(const YAML::Node& config);

// The prompt and its non-template bytes: request content is foreign, each model reply is one verified span.
struct RenderedPrompt {
  std::string text;
  std::vector<TextSpan> spans;
};

class PromptTemplate {
public:
  explicit PromptTemplate(PromptTemplateParams params);

  RenderedPrompt RenderMarked(const AssistantRequest& request) const;
  std::string Render(const AssistantRequest& request) const {
    return RenderMarked(request).text;
  }

  const PromptTemplateParams& Params() const {
    return params_;
  }

private:
  PromptTemplateParams params_;
};

} // namespace giga_edge
