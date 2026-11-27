#include "submodel_graph_builder.h"

#include "giga_edge/lib/common/run_in_parallel.h"
#include "giga_edge/lib/common/stderr_line.h"

#include <chrono>
#include <sstream>
#include <string>

namespace giga_edge {

RuntimeManager::SubmodelGraphBuilder::SubmodelGraphBuilder(
  const RuntimeManager& manager, Runtime& model, CurrentState* current_state, std::vector<BuildFailure>* failures
)
  : manager_(manager)
  , model_(model)
  , current_state_(current_state)
  , failures_(failures) {}

bool RuntimeManager::SubmodelGraphBuilder::Add(
  std::vector<SubmodelVersionChain>& active_versions,
  CurrentState& active_current_state,
  const VersionToSwitch& version_to_switch
) {
  const auto& version_chain = version_to_switch.version_chain;
  const bool is_something_new = version_to_switch.version_from.empty();
  const auto& state = version_to_switch.version_to;

  if (version_chain.ids_chain.empty()) {
    WriteStderrLine("[WARNING] Cannot add submodel version for an empty version chain\n");
    return false;
  }
  if (!version_chain.ids_chain.back().version.empty()) {
    WriteStderrLine(
      "[WARNING] Cannot add submodel version for resolved version chain '" + version_chain.AsString() + "'\n"
    );
    return false;
  }

  auto current_version_chain = version_chain;
  const auto& version = state.current_version;

  try {
    const auto& version_part_id = manager_.version_to_part_.at(version);
    const auto& part = manager_.runtime_parts_.at(version_part_id);

    current_version_chain.ids_chain.back().version = version;
    const auto load_start = std::chrono::steady_clock::now();
    model_.AddSubmodelVersion(current_version_chain, part.data_reader, part.available_versions.at(version).config_path);
    const std::chrono::duration<double, std::milli> load_ms = std::chrono::steady_clock::now() - load_start;
    std::ostringstream line;
    line << "[load] version " << current_version_chain.AsString() << " ms=" << static_cast<long>(load_ms.count())
         << '\n';
    WriteStderrLine(line.str());
  } catch (std::exception& error) {
    std::ostringstream line;
    line << "[WARNING] Failed to add submodel version (version chain: \"" << version_chain.AsString()
         << "\", version: \"" << version << "\"): " << error.what() << '\n';
    WriteStderrLine(line.str());
    if (failures_ != nullptr) {
      auto failed_chain = version_chain;
      failed_chain.ids_chain.back().version = version;
      failures_->push_back({.version_chain = std::move(failed_chain), .required_version = state.required_version});
    }
    return is_something_new ? AddDefaultVersion(active_versions, active_current_state, version_to_switch) : false;
  }

  std::vector<SubmodelVersionChain> local_active_versions{current_version_chain};
  CurrentState local_active_current_state{{version_chain, state}};

  // Submodels of one level are independent: build them at once, the results are merged in the traversal order of
  // required_versions.
  struct ChildResult {
    std::vector<SubmodelVersionChain> active_versions;
    CurrentState active_current_state;
    std::vector<BuildFailure> failures;
    bool added = false;
  };
  std::vector<ChildResult> results;
  try {
    const auto new_version = model_.Core()->SubmodelVersion(current_version_chain);
    std::vector<SubmodelVersionChain> required_versions;
    new_version->GatherRequiredVersions(required_versions, manager_.feature_manager_.GetFeatures());
    const auto default_versions = new_version->DefaultSubmodels();
    current_version_chain.ids_chain.emplace_back();

    results.resize(required_versions.size());
    RunInParallel(required_versions.size(), static_cast<std::size_t>(model_.LoadThreads()), [&](std::size_t i) {
      // The version is resolved inside the job: with one thread it is not looked at after a sibling fails.
      const auto& submodel_id = required_versions[i].ids_chain.back().id;
      const auto& required_submodel_version = required_versions[i].ids_chain.back().version;
      auto child_chain = current_version_chain;
      child_chain.ids_chain.back().id = submodel_id;
      const VersionToSwitch child = {
        .version_chain = child_chain,
        .version_from = CurrentVersion(child_chain),
        .version_to = {
          .current_version = manager_.version_to_part_.count(required_submodel_version)
            ? required_submodel_version
            : default_versions.at(submodel_id).submodel_version,
          .required_version = required_submodel_version,
        },
      };
      // Failures of a branch are collected separately and reach the shared list below: as in a loop, up to the first
      // one that was not built.
      SubmodelGraphBuilder branch(
        manager_, model_, current_state_, failures_ == nullptr ? nullptr : &results[i].failures
      );
      results[i].added = branch.Add(results[i].active_versions, results[i].active_current_state, child);
      return results[i].added;
    });
  } catch (...) {
    // The version is already in the model: unless it is removed, the next UpdateState would skip it as existing.
    Remove(local_active_versions.front());
    throw;
  }
  for (auto& result : results) {
    if (failures_ != nullptr) {
      failures_->insert(failures_->end(), result.failures.begin(), result.failures.end());
    }
    if (!result.added) {
      Remove(local_active_versions.front());
      return is_something_new ? AddDefaultVersion(active_versions, active_current_state, version_to_switch) : false;
    }
    local_active_versions.insert(
      local_active_versions.end(), result.active_versions.begin(), result.active_versions.end()
    );
    for (auto& [chain, child_state] : result.active_current_state) {
      local_active_current_state[chain] = std::move(child_state);
    }
  }

  active_versions.insert(active_versions.end(), local_active_versions.begin(), local_active_versions.end());
  for (const auto& [chain, current_state] : local_active_current_state) {
    active_current_state[chain] = current_state;
  }
  return true;
}

bool RuntimeManager::SubmodelGraphBuilder::AddDefaultVersion(
  std::vector<SubmodelVersionChain>& active_versions,
  CurrentState& active_current_state,
  const VersionToSwitch& version_to_switch
) {
  const auto& version_chain = version_to_switch.version_chain;
  const auto& state = version_to_switch.version_to;
  const auto& submodel_id = version_chain.ids_chain.back().id;
  const auto& default_versions = model_.Core()->SubmodelVersion(version_chain.PopBack())->DefaultSubmodels();
  if (default_versions.find(submodel_id) == default_versions.end()) {
    WriteStderrLine(
      "[WARNING] Failed to apply default version due to it is not declared (submodel id: \"" + submodel_id + "\")\n"
    );
    return false;
  }

  const auto& default_version = default_versions.at(submodel_id).submodel_version;
  if (default_version == state.current_version) {
    return false;
  }

  return Add(
    active_versions,
    active_current_state,
    {
      .version_chain = version_chain,
      .version_from = "",
      .version_to = {.current_version = default_version, .required_version = state.required_version},
    }
  );
}

std::string RuntimeManager::SubmodelGraphBuilder::CurrentVersion(const SubmodelVersionChain& version_chain) const {
  if (current_state_ == nullptr) {
    return {};
  }
  const std::lock_guard lock(manager_.build_state_mutex_);
  const auto it = current_state_->find(version_chain);
  return it == current_state_->end() ? "" : it->second.current_version;
}

void RuntimeManager::SubmodelGraphBuilder::Remove(const SubmodelVersionChain& version_chain) {
  model_.RemoveSubmodelVersion(version_chain);
  if (current_state_ == nullptr) {
    return;
  }
  const std::lock_guard lock(manager_.build_state_mutex_);
  for (auto it = current_state_->begin(); it != current_state_->end();
       it = version_chain.IsPrefixOf(it->first) ? current_state_->erase(it) : ++it);
}

} // namespace giga_edge
