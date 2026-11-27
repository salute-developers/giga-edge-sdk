#include "giga_edge/modules/assistant_core/prompt_template.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace giga_edge {
namespace {

void ParseRole(const YAML::Node& node, RoleFormat& format) {
  if (!node) {
    return;
  }
  format.prefix = node["prefix"].as<std::string>(format.prefix);
  format.suffix = node["suffix"].as<std::string>(format.suffix);
}

} // namespace

PromptTemplateParams ParsePromptTemplateParams(const YAML::Node& config) {
  if (config["preset"]) {
    throw std::invalid_argument("Prompt template presets are not supported: describe the roles in the model config");
  }
  PromptTemplateParams p;
  ParseRole(config["system"], p.system);
  ParseRole(config["scene"], p.scene);
  ParseRole(config["user"], p.user);
  ParseRole(config["assistant"], p.assistant);
  ParseRole(config["tool"], p.tool);
  p.generation_prefix = config["generation_prefix"].as<std::string>(p.generation_prefix);
  p.system_instructions = config["system_instructions"].as<std::string>(p.system_instructions);
  p.render_empty_system = config["render_empty_system"].as<bool>(p.render_empty_system);
  p.render_empty_scene = config["render_empty_scene"].as<bool>(p.render_empty_scene);
  if (const auto scene = config["scene"]; scene) {
    p.scene_literal = scene["literal"].as<bool>(p.scene_literal);
  }
  p.scene_in_user = config["scene_in_user"].as<bool>(p.scene_in_user);
  p.render_tools = config["render_tools"].as<bool>(p.render_tools);
  p.tools_header = config["tools_header"].as<std::string>(p.tools_header);
  p.tools_footer = config["tools_footer"].as<std::string>(p.tools_footer);
  p.tool_call_prefix = config["tool_call_prefix"].as<std::string>(p.tool_call_prefix);
  p.tool_call_suffix = config["tool_call_suffix"].as<std::string>(p.tool_call_suffix);
  return p;
}

PromptTemplate::PromptTemplate(PromptTemplateParams params)
  : params_(std::move(params)) {}

namespace {

// The prompt string; bytes added by Text are marked with a span.
class PromptBuilder {
public:
  void Template(std::string_view text) {
    text_ += text;
  }

  void Text(std::string_view text, TextOrigin origin) {
    if (!text.empty()) {
      spans_.push_back({text_.size(), text_.size() + text.size(), origin});
    }
    text_ += text;
  }

  void Append(const PromptBuilder& other) {
    for (auto span : other.spans_) {
      span.begin += text_.size();
      span.end += text_.size();
      spans_.push_back(span);
    }
    text_ += other.text_;
  }

  // Bytes from size onwards come from the template.
  void TrustFrom(size_t size) {
    while (!spans_.empty() && spans_.back().begin >= size) {
      spans_.pop_back();
    }
    if (!spans_.empty()) {
      spans_.back().end = std::min(spans_.back().end, size);
    }
  }

  const std::string& Str() const {
    return text_;
  }

  RenderedPrompt Take() {
    return {std::move(text_), std::move(spans_)};
  }

private:
  std::string text_;
  std::vector<TextSpan> spans_;
};

PromptBuilder RenderTools(const PromptTemplateParams& params, const std::vector<ToolSpec>& tools) {
  PromptBuilder out;
  if (!params.render_tools || tools.empty()) {
    return out;
  }
  out.Template(params.tools_header);
  for (const auto& tool : tools) {
    out.Template("\n{\"type\": \"function\", \"function\": {\"name\": ");
    out.Text(JsonQuote(tool.name), TextOrigin::FOREIGN);
    out.Template(", \"description\": ");
    out.Text(JsonQuote(tool.description), TextOrigin::FOREIGN);
    if (!tool.parameters_json.empty()) {
      out.Template(", \"parameters\": ");
      out.Text(tool.parameters_json, TextOrigin::FOREIGN);
    }
    out.Template("}}");
  }
  out.Template(params.tools_footer);
  return out;
}

void RenderScene(const PromptTemplateParams& params, const AssistantMessage& scene, PromptBuilder& out) {
  out.Template(params.scene.prefix);
  if (params.scene_literal) {
    out.Text(scene.content, TextOrigin::FOREIGN);
  } else {
    out.Template(scene.content);
  }
  out.Template(params.scene.suffix);
}

PromptBuilder RenderAssistant(const PromptTemplateParams& params, const AssistantMessage& message) {
  PromptBuilder body;
  if (!message.content.empty()) {
    body.Text(message.content, TextOrigin::VERIFIED);
  } else {
    for (const auto& call : message.tool_calls) {
      if (!call.raw_text.empty()) {
        body.Text(call.raw_text, TextOrigin::VERIFIED);
        continue;
      }
      body.Template(params.tool_call_prefix + "{\"name\": ");
      body.Text(JsonQuote(call.name), TextOrigin::FOREIGN);
      body.Template(", \"arguments\": ");
      body.Text(call.arguments_json.empty() ? "{}" : call.arguments_json, TextOrigin::FOREIGN);
      body.Template("}" + params.tool_call_suffix);
    }
  }
  // The model appended the beginning of the suffix: the template puts it in its proper place.
  const size_t overlap = SuffixPrefixOverlap(body.Str(), params.assistant.suffix);
  body.TrustFrom(body.Str().size() - overlap);
  PromptBuilder out;
  out.Template(params.assistant.prefix);
  out.Append(body);
  out.Template(std::string_view(params.assistant.suffix).substr(overlap));
  return out;
}

void RenderMessage(
  const PromptTemplateParams& params,
  const AssistantMessage& message,
  const PromptBuilder& tools_block,
  bool& tools_attached,
  PromptBuilder& out
) {
  switch (message.role) {
    case MessageRole::SYSTEM: {
      PromptBuilder content;
      content.Template(params.system_instructions);
      if (!message.content.empty()) {
        content.Template(params.system_instructions.empty() ? "" : "\n");
        content.Text(message.content, TextOrigin::FOREIGN);
      }
      if (!tools_block.Str().empty() && !tools_attached) {
        content.Append(tools_block);
        tools_attached = true;
      }
      if (content.Str().empty() && !params.render_empty_system) {
        return;
      }
      out.Template(params.system.prefix);
      out.Append(content);
      out.Template(params.system.suffix);
      return;
    }
    case MessageRole::SCENE:
      if (message.content.empty() && !params.render_empty_scene) {
        return;
      }
      RenderScene(params, message, out);
      return;
    case MessageRole::USER:
      out.Template(params.user.prefix);
      out.Text(message.content, TextOrigin::FOREIGN);
      out.Template(params.user.suffix);
      return;
    case MessageRole::ASSISTANT:
      out.Append(RenderAssistant(params, message));
      return;
    case MessageRole::TOOL:
      out.Template(params.tool.prefix);
      out.Text(message.content, TextOrigin::FOREIGN);
      out.Template(params.tool.suffix);
      return;
  }
}

} // namespace

RenderedPrompt PromptTemplate::RenderMarked(const AssistantRequest& request) const {
  const auto tools_block = RenderTools(params_, request.tools);
  bool tools_attached = false;
  PromptBuilder body;
  const AssistantMessage* scene = nullptr;
  for (const auto& message : request.messages) {
    if (params_.scene_in_user && message.role == MessageRole::SCENE) {
      if (!message.content.empty() || params_.render_empty_scene) {
        scene = &message;
      }
      continue;
    }
    if (scene != nullptr && message.role == MessageRole::USER) {
      body.Template(params_.user.prefix);
      RenderScene(params_, *scene, body);
      body.Text(message.content, TextOrigin::FOREIGN);
      body.Template(params_.user.suffix);
      scene = nullptr;
      continue;
    }
    RenderMessage(params_, message, tools_block, tools_attached, body);
  }
  if (scene != nullptr) {
    body.Template(params_.user.prefix);
    RenderScene(params_, *scene, body);
  }
  PromptBuilder out;
  if (!tools_block.Str().empty() && !tools_attached) {
    out.Template(params_.system.prefix);
    out.Append(tools_block);
    out.Template(params_.system.suffix);
  }
  out.Append(body);
  // A warm-up computes the common prefix of the next requests: no generation follows it.
  if (request.kind != AssistantRequestKind::WARM) {
    out.Template(params_.generation_prefix);
  }
  return out.Take();
}

} // namespace giga_edge
