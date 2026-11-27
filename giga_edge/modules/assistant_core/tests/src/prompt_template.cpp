#include "giga_edge/modules/assistant_core/prompt_template.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;

namespace {

const std::string kScene
  = "<d_hvac_underfloor_heating>0 Тёплый пол в гостиной "
    "[Гостиная]<p_ro_online><p_ro_temperature><p_rw_hvac_thermostat_mode><p_rw_hvac_temp_set><p_rw_on_off>\n<d_led_"
    "strip>0 Светодиодная лента в гостиной "
    "[Гостиная]<p_ro_online><p_rw_light_mode><p_rw_light_colour><p_rw_light_brightness><p_rw_on_off>\n<d_led_strip>1 "
    "Светодиодная лента на кухне "
    "[Кухня]<p_ro_online><p_rw_light_mode><p_rw_light_colour><p_rw_light_brightness><p_rw_on_off>";
const std::string kStatic
  = "system<|role_sep|>\n<|message_sep|>\n\nuser<|role_sep|>\n<d_hvac_underfloor_heating>0 Тёплый пол в гостиной "
    "[Гостиная]<p_ro_online><p_ro_temperature><p_rw_hvac_thermostat_mode><p_rw_hvac_temp_set><p_rw_on_off>\n<d_led_"
    "strip>0 Светодиодная лента в гостиной "
    "[Гостиная]<p_ro_online><p_rw_light_mode><p_rw_light_colour><p_rw_light_brightness><p_rw_on_off>\n<d_led_strip>1 "
    "Светодиодная лента на кухне "
    "[Кухня]<p_ro_online><p_rw_light_mode><p_rw_light_colour><p_rw_light_brightness><p_rw_on_off><sep>";
const std::string kTurn0 = "включи свет в гостиной<|message_sep|>\n\nassistant<|role_sep|>\n";
const std::string kCall0
  = "<start_function_call><f_set_device_parameter><d_led_strip>0<arg_sep><p_rw_on_off><arg_sep>true";
const std::string kFollowup1
  = "function results<|role_sep|>\n{\"status\": \"success\"}<|message_sep|>\n\nassistant<|role_sep|>\n";
const std::string kAnswer1 = "Включил свет в гостиной.";
const std::string kTurn1 = "какая яркость света в гостиной<|message_sep|>\n\nassistant<|role_sep|>\n";
const std::string kCall2
  = "<start_function_call><f_get_device_parameter><d_led_strip>0<arg_sep><p_rw_light_brightness>";
const std::string kFollowup2
  = "function results<|role_sep|>\n{\"status\": \"success\", \"value\": 70}<|message_sep|>\n\nassistant<|role_sep|>\n";
const std::string kCall3
  = "<start_function_call><f_get_device_parameter><d_led_strip>1<arg_sep><p_rw_light_brightness>";
const std::string kFollowup3
  = "function results<|role_sep|>\n{\"status\": \"success\", \"value\": 40}<|message_sep|>\n\nassistant<|role_sep|>\n";
const std::string kStop = "<|message_sep|>\n\n";

AssistantMessage Msg(MessageRole role, std::string content) {
  AssistantMessage m;
  m.role = role;
  m.content = std::move(content);
  return m;
}

PromptTemplate Template(const char* yaml) {
  return PromptTemplate(ParsePromptTemplateParams(YAML::Load(yaml)));
}

// The same template as in the config of the function calling model.
PromptTemplate Compact() {
  return Template(R"(
    system: {prefix: "system<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    scene: {prefix: "", suffix: "<sep>"}
    user: {prefix: "user<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    assistant: {prefix: "assistant<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    tool: {prefix: "function results<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    generation_prefix: "assistant<|role_sep|>\n"
    render_empty_system: true
    render_empty_scene: true
    scene_in_user: true
  )");
}

PromptTemplate ChatMl() {
  return Template(R"(
    system: {prefix: "<|im_start|>system\n", suffix: "<|im_end|>\n"}
    scene: {prefix: "<|im_start|>user\n", suffix: "<|im_end|>\n"}
    user: {prefix: "<|im_start|>user\n", suffix: "<|im_end|>\n"}
    assistant: {prefix: "<|im_start|>assistant\n", suffix: "<|im_end|>\n"}
    tool: {prefix: "<|im_start|>user\n<tool_response>\n", suffix: "\n</tool_response><|im_end|>\n"}
    generation_prefix: "<|im_start|>assistant\n"
    render_tools: true
    tools_header: "\n\n# Tools\n\n<tools>"
    tools_footer: "\n</tools>"
    tool_call_prefix: "<tool_call>\n"
    tool_call_suffix: "\n</tool_call>"
  )");
}

AssistantRequest CompactRequest(const std::string& user) {
  AssistantRequest r;
  r.kind = AssistantRequestKind::GENERATE;
  r.messages.push_back(Msg(MessageRole::SYSTEM, ""));
  r.messages.push_back(Msg(MessageRole::SCENE, kScene));
  r.messages.push_back(Msg(MessageRole::USER, user));
  return r;
}

} // namespace

TEST_CASE("compact template renders the release prompt byte for byte", "[assistant][template][compact]") {
  const auto tpl = Compact();
  REQUIRE(tpl.Render(CompactRequest("включи свет в гостиной")) == kStatic + kTurn0);
}

TEST_CASE("compact template retains the user role on an empty scene", "[assistant][template][compact]") {
  auto request = CompactRequest("Включи свет");
  request.messages[1].content.clear();
  const auto compact = Compact();
  REQUIRE(compact.Render(request) == "system<|role_sep|>\n<|message_sep|>\n\nuser<|role_sep|>\n<sep>Включи свет<|message_sep|>\n\nassistant<|role_sep|>\n");
  request.messages.pop_back();
  request.kind = AssistantRequestKind::WARM;
  REQUIRE(compact.Render(request).find("user<|role_sep|>\n<sep>") != std::string::npos);
  const auto chatml = ChatMl();
  REQUIRE(chatml.Render(request).find("<|im_start|>user") == std::string::npos);
}

TEST_CASE(
  "compact template renders a tool hop: the call, its result and the head of the next reply",
  "[assistant][template][compact]"
) {
  const auto tpl = Compact();
  auto r = CompactRequest("включи свет в гостиной");
  r.messages.push_back(Msg(MessageRole::ASSISTANT, kCall0));
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"status\": \"success\"}"));
  REQUIRE(tpl.Render(r) == kStatic + kTurn0 + kCall0 + kStop + kFollowup1);
}

TEST_CASE("compact template renders a two-call chain", "[assistant][template][compact]") {
  const auto tpl = Compact();
  auto r = CompactRequest("какая яркость света в гостиной");
  r.messages.push_back(Msg(MessageRole::ASSISTANT, kCall2));
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"status\": \"success\", \"value\": 70}"));
  r.messages.push_back(Msg(MessageRole::ASSISTANT, kCall3));
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"status\": \"success\", \"value\": 40}"));
  REQUIRE(tpl.Render(r) == kStatic + kTurn1 + kCall2 + kStop + kFollowup2 + kCall3 + kStop + kFollowup3);
}

TEST_CASE("compact template renders history as the trained multi-turn format", "[assistant][template][compact]") {
  const auto tpl = Compact();
  auto r = CompactRequest("включи свет в гостиной");
  r.messages.push_back(Msg(MessageRole::ASSISTANT, kCall0));
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"status\": \"success\"}"));
  r.messages.push_back(Msg(MessageRole::ASSISTANT, kAnswer1));
  r.messages.push_back(Msg(MessageRole::USER, "какая яркость света в гостиной"));
  const std::string history = kStatic + kTurn0 + kCall0 + kStop + kFollowup1 + kAnswer1 + kStop;
  REQUIRE(tpl.Render(r) == history + "user<|role_sep|>\n" + kTurn1);

  r.messages.push_back(Msg(MessageRole::ASSISTANT, kCall2));
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"status\": \"success\", \"value\": 70}"));
  REQUIRE(tpl.Render(r) == history + "user<|role_sep|>\n" + kTurn1 + kCall2 + kStop + kFollowup2);

  AssistantRequest warm;
  warm.kind = AssistantRequestKind::WARM;
  warm.messages = {r.messages[0], r.messages[1]};
  REQUIRE(tpl.Render(warm) == kStatic);
}

TEST_CASE("assistant suffix is not doubled when the model emitted part of the stop sequence", "[assistant][template]") {
  const auto tpl = Template("{assistant: {prefix: \"A:\", suffix: \"<|message_sep|>\\n\\n\"}}");
  const auto rendered = [&tpl](const std::string& answer) {
    AssistantRequest r;
    r.kind = AssistantRequestKind::WARM;
    r.messages.push_back(Msg(MessageRole::ASSISTANT, answer));
    return tpl.Render(r);
  };
  REQUIRE(rendered("abc") == "A:abc<|message_sep|>\n\n");
  REQUIRE(rendered("abc<|message") == "A:abc<|message_sep|>\n\n");
  REQUIRE(rendered("abc<|message_sep|>") == "A:abc<|message_sep|>\n\n");
  REQUIRE(rendered("abc<|message_sep|>\n\n") == "A:abc<|message_sep|>\n\n");
}

TEST_CASE("chatml template renders system, tools, history and generation prefix", "[assistant][template][chatml]") {
  const auto tpl = ChatMl();
  AssistantRequest r;
  r.messages.push_back(Msg(MessageRole::SYSTEM, "be brief"));
  r.messages.push_back(Msg(MessageRole::USER, "hi"));
  r.messages.push_back(Msg(MessageRole::ASSISTANT, "hello"));
  r.messages.push_back(Msg(MessageRole::USER, "call it"));
  r.tools.push_back(ToolSpec{"ping", "check", "{\"type\": \"object\"}"});
  const auto text = tpl.Render(r);
  REQUIRE(text.rfind("<|im_start|>system\nbe brief\n\n# Tools", 0) == 0);
  REQUIRE(
    text.find(
      "{\"type\": \"function\", \"function\": {\"name\": \"ping\", \"description\": \"check\", \"parameters\": "
      "{\"type\": \"object\"}}}"
    )
    != std::string::npos
  );
  REQUIRE(text.find("<|im_start|>user\nhi<|im_end|>\n<|im_start|>assistant\nhello<|im_end|>\n") != std::string::npos);
  const std::string tail = "<|im_start|>user\ncall it<|im_end|>\n<|im_start|>assistant\n";
  REQUIRE(text.compare(text.size() - tail.size(), tail.size(), tail) == 0);
}

TEST_CASE("tools go before the dialogue when the request has no system message", "[assistant][template]") {
  const auto tpl = ChatMl();
  AssistantRequest r;
  r.messages.push_back({MessageRole::USER, "hi"});
  r.tools.push_back(ToolSpec{"ping", "check", ""});
  const auto out = tpl.Render(r);
  REQUIRE(out.find("<tools>") < out.find("<|im_start|>user\nhi<|im_end|>"));
  REQUIRE(out.rfind("<|im_start|>system\n", 0) == 0);
}

TEST_CASE("chatml template renders structured tool calls and results", "[assistant][template][chatml]") {
  const auto tpl = ChatMl();
  AssistantRequest r;
  r.messages.push_back(Msg(MessageRole::USER, "send"));
  AssistantMessage call = Msg(MessageRole::ASSISTANT, "");
  call.tool_calls.push_back(ToolCall{.name = "send", .arguments_json = "{\"to\": \"x\"}"});
  r.messages.push_back(call);
  r.messages.push_back(Msg(MessageRole::TOOL, "{\"ok\": true}"));
  REQUIRE(
    tpl.Render(r)
    == "<|im_start|>user\nsend<|im_end|>\n<|im_start|>assistant\n<tool_call>\n{\"name\": \"send\", \"arguments\": {\"to\": \"x\"}}\n</tool_call><|im_end|>\n"
       "<|im_start|>user\n<tool_response>\n{\"ok\": true}\n</tool_response><|im_end|>\n<|im_start|>assistant\n"
  );
}

TEST_CASE("template is described entirely by the config", "[assistant][template]") {
  const auto tpl = Template(R"(
    system: {prefix: "S:", suffix: "|"}
    user: {prefix: "U:", suffix: "\n"}
    generation_prefix: "A:"
    system_instructions: "you are a router"
  )");
  AssistantRequest r;
  r.messages.push_back(Msg(MessageRole::SYSTEM, "rules"));
  r.messages.push_back(Msg(MessageRole::USER, "q"));
  r.tools.push_back(ToolSpec{"t", "d", ""});
  REQUIRE(tpl.Render(r) == "S:you are a router\nrules|U:q\nA:");
  REQUIRE(Template("{}").Render(r) == "rulesq");
  REQUIRE_THROWS(ParsePromptTemplateParams(YAML::Load("preset: chatml")));
}

TEST_CASE("empty system and scene are skipped unless the preset renders them", "[assistant][template]") {
  const auto chatml = ChatMl();
  AssistantRequest r;
  r.messages.push_back(Msg(MessageRole::SYSTEM, ""));
  r.messages.push_back(Msg(MessageRole::SCENE, ""));
  r.messages.push_back(Msg(MessageRole::USER, "q"));
  REQUIRE(chatml.Render(r) == "<|im_start|>user\nq<|im_end|>\n<|im_start|>assistant\n");
  const auto compact = Compact();
  REQUIRE(
    compact.Render(r)
    == "system<|role_sep|>\n<|message_sep|>\n\nuser<|role_sep|>\n<sep>q<|message_sep|>\n\nassistant<|role_sep|>\n"
  );
}

namespace {

// Foreign bytes are wrapped in guillemets, verified pieces in [[...]].
std::string Marked(const RenderedPrompt& prompt) {
  std::string out;
  size_t at = 0;
  for (const auto& span : prompt.spans) {
    REQUIRE(span.begin >= at);
    REQUIRE(span.end > span.begin);
    REQUIRE(span.end <= prompt.text.size());
    const bool foreign = span.origin == TextOrigin::FOREIGN;
    out += prompt.text.substr(at, span.begin - at) + (foreign ? "«" : "[[")
      + prompt.text.substr(span.begin, span.end - span.begin) + (foreign ? "»" : "]]");
    at = span.end;
  }
  return out + prompt.text.substr(at);
}

AssistantMessage Message(MessageRole role, std::string content) {
  AssistantMessage m;
  m.role = role;
  m.content = std::move(content);
  return m;
}

} // namespace

TEST_CASE(
  "Prompt template marks request text as foreign and the model's answer as verified",
  "[assistant_core][prompt_template]"
) {
  const auto tpl = Template(R"({
    system: {prefix: "S:", suffix: "|"}, user: {prefix: "U:", suffix: "|"}, assistant: {prefix: "A:", suffix: "</a>"},
    tool: {prefix: "T:", suffix: "|"}, scene: {prefix: "<scene>", suffix: "</scene>"}, generation_prefix: "A:",
    system_instructions: "rules"})");
  AssistantRequest r;
  r.kind = AssistantRequestKind::GENERATE;
  r.messages = {
    Message(MessageRole::SYSTEM, "host"),
    Message(MessageRole::SCENE, "<d_light>0"),
    Message(MessageRole::USER, "q"),
    Message(MessageRole::ASSISTANT, "call</"),
    Message(MessageRole::TOOL, "ok"),
  };
  const auto rendered = tpl.RenderMarked(r);
  REQUIRE(rendered.text == tpl.Render(r));
  REQUIRE(Marked(rendered) == "S:rules\n«host»|<scene><d_light>0</scene>U:«q»|A:[[call]]</a>T:«ok»|A:");
}

TEST_CASE(
  "Prompt template marks tools, synthesized calls and a scene inside the user turn", "[assistant_core][prompt_template]"
) {
  auto tpl = Template(R"({
    system: {prefix: "S:", suffix: "|"}, user: {prefix: "U:", suffix: "|"}, assistant: {prefix: "A:", suffix: "|"},
    scene: {prefix: "", suffix: "<sep>", literal: true}, scene_in_user: true, render_tools: true, tools_header: "TOOLS",
    tool_call_prefix: "<call>", generation_prefix: "A:"})");
  AssistantRequest r;
  r.kind = AssistantRequestKind::GENERATE;
  r.tools = {{"lamp", "turns", ""}};
  AssistantMessage call = Message(MessageRole::ASSISTANT, "");
  call.tool_calls = {{"", "lamp", "{}", ""}, {"", "x", "", "<raw>"}};
  r.messages = {Message(MessageRole::SCENE, "catalog"), Message(MessageRole::USER, "q"), call};
  REQUIRE(
    Marked(tpl.RenderMarked(r))
    == "S:TOOLS\n{\"type\": \"function\", \"function\": {\"name\": «\"lamp\"», \"description\": «\"turns\"»}}|U:«catalog»<sep>«q»|"
       "A:<call>{\"name\": «\"lamp\"», \"arguments\": «{}»}[[<raw>]]|A:"
  );
}
