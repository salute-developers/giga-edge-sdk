#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"

#include <catch.hpp>

#include <any>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace giga_edge;
using namespace std::chrono_literals;

namespace {

IDataSpanPtr MakeDataSpan(std::string_view text) {
  return Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end()));
}

struct StickyTextEvent {
  std::string text;
};

class EventRecorderNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {}

  void ProcessEventImpl(const std::any& event) override {
    if (const auto* sticky = std::any_cast<StickyTextEvent>(&event)) {
      last_text_ = sticky->text;
    }
  }

  void ProcessOnce() override {
    (void)GetTrigger();
    PushValue(last_text_);
  }

  REGISTER_INPUT(event_recorder_node, trigger, Trigger, int32_t, "")
  REGISTER_OUTPUT(event_recorder_node, value, Value, std::string, "")

private:
  std::string last_text_;
};

REGISTER_NODE(event_recorder_node, EventRecorderNode, "");

} // namespace

TEST_CASE("Submodels, single version switch replays cached events", "[runtime][submodels]") {
  auto data_reader = CreateCachingDataReader(
    nullptr,
    {
      {"config_core.yaml", MakeDataSpan(R"(
num_threads: 0
version: test
lib_version: 1.0

submodels:
  - id: rec
    inputs:
      trigger: trigger
    outputs:
      value: rec_value
)")},
      {"config_recorder.yaml", MakeDataSpan(R"(
nodes:
  - type: event_recorder_node
    name: recorder
    inputs:
      trigger: trigger
    outputs:
      value: value
)")},
    }
  );

  Runtime model(data_reader, "config_core.yaml");
  const SubmodelVersionChain v1{.ids_chain = {{.id = "rec", .version = "v1"}}};
  const SubmodelVersionChain v2{.ids_chain = {{.id = "rec", .version = "v2"}}};
  model.AddSubmodelVersion(v1, data_reader, "config_recorder.yaml");
  model.SetActiveSubmodelVersion(v1);

  std::vector<std::string> v1_values;
  model.Subscribe<std::string>(v1, "value", [&v1_values](const std::string& value, const std::any&) {
    v1_values.push_back(value);
  });

  model.PushEvent(
    Event{.event = StickyTextEvent{.text = "hello"}},
    EventCacheAction{.action = EventCacheAction::Action::SET, .key = "sticky_text"}
  );
  model.Push(SubmodelVersionChain{}, "trigger", int32_t{1}, 0);
  while (!model.Empty()) {
    std::this_thread::sleep_for(10ms);
  }
  REQUIRE(v1_values == std::vector<std::string>{"hello"});

  model.AddSubmodelVersion(v2, data_reader, "config_recorder.yaml");
  std::vector<std::string> v2_values;
  model.Subscribe<std::string>(v2, "value", [&v2_values](const std::string& value, const std::any&) {
    v2_values.push_back(value);
  });

  model.SetActiveSubmodelVersion(v2);
  model.Push(SubmodelVersionChain{}, "trigger", int32_t{1}, 1);
  while (!model.Empty()) {
    std::this_thread::sleep_for(10ms);
  }
  REQUIRE(v2_values == std::vector<std::string>{"hello"});
}
