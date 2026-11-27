#include "giga_edge/lib/environment/environment.h"

#include "temp_dir.h"

#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/lib/threadpool/threadpool.h"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace giga_edge;
using tests::TempModelDir;
using tests::WriteConfig;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> pool_task_ran{false};

class PoolProbeNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  explicit PoolProbeNode(const NodeBaseConfig& config)
    : NodeBase(config) {
    const auto environment = config.environments->find("probe_pool");
    if (environment == config.environments->end()) {
      throw std::runtime_error("pool_probe_node requires the probe_pool environment");
    }
    pool_ = std::dynamic_pointer_cast<ThreadPoolEnvironment>(environment->second)->thread_pool;
  }

  void Reset() override {}

  void ProcessOnce() override {
    (void)GetTrigger();
    pool_->Push(
      std::make_shared<Task>(
        []() {
          pool_task_ran = true;
        },
        0
      )
    );
    PushValue(int32_t{0});
  }

  REGISTER_INPUT(pool_probe_node, trigger, Trigger, int32_t, "")
  REGISTER_OUTPUT(pool_probe_node, value, Value, int32_t, "")

private:
  std::shared_ptr<IThreadPool> pool_;
};

REGISTER_NODE(pool_probe_node, PoolProbeNode, "");

} // namespace

TEST_CASE("Thread pool environment without a thread_pool section runs tasks", "[runtime][runtime_manager]") {
  TempModelDir tmp;
  WriteConfig(
    tmp.path / "core",
    R"(num_threads: 1
version: test
lib_version: 1.18

model_environments:
  probe_pool:
    type: thread_pool

nodes:
  - type: pool_probe_node
    name: probe
    inputs:
      trigger: trigger
    outputs:
      value: value
)"
  );

  RuntimeManager manager(
    RuntimePart{
      .id = "core",
      .data = ModelFileData{(tmp.path / "core").string()},
      .versions = {{"core", {.config_path = "config.yaml"}}},
    },
    {},
    {},
    {}
  );

  auto model = manager.Model();
  model->Push<int32_t>({}, "trigger", int32_t{1}, 0);
  while (!model->Empty()) {
    std::this_thread::sleep_for(10ms);
  }

  for (int i = 0; i < 500 && !pool_task_ran.load(); ++i) {
    std::this_thread::sleep_for(10ms);
  }
  REQUIRE(pool_task_ran.load());
}
