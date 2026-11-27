#include "giga_edge/lib/runtime_manager/runtime_manager.h"

#include "submodel_graph_builder.h"

#include "giga_edge/lib/common/overloaded.h"
#include "giga_edge/lib/common/run_in_parallel.h"
#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/runtime/params.h"

#include <chrono>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace giga_edge {
namespace {

// The temporary tar reader hands out spans into the holder's buffer without copying: the holder must
// live as long as any client of the reader does. The members are declared so that reader is destroyed first.
struct HoldingTarBufferOwner {
  ITemporaryDataHolderPtr holder;
  IDataReaderPtr reader;
};

IDataReaderPtr CreateHoldingTarBufferDataReader(ITemporaryDataHolderPtr data, const DataReaderOptions& options) {
  auto [bytes, size] = data->Data();
  // Not inside the aggregate: GCC < 13 leaks already built members when a later initializer throws.
  auto reader = CreateTemporaryTarBufferDataReader(bytes, size, options);
  auto owner = std::make_shared<HoldingTarBufferOwner>(HoldingTarBufferOwner{std::move(data), std::move(reader)});
  // Aliasing to the reader itself: the whole IDataReader interface is forwarded without a wrapper.
  return {owner, owner->reader.get()};
}

} // namespace

bool RuntimeManager::SubmodelVersionState::operator==(const SubmodelVersionState& other) {
  return current_version == other.current_version && required_version == other.required_version;
}

bool RuntimeManager::SubmodelVersionState::operator!=(const SubmodelVersionState& other) {
  return !(*this == other);
}

RuntimeManager::RuntimeManager(
  const RuntimePart& core,
  const std::vector<RuntimePart>& parts,
  EnvironmentsMap environments,
  std::unordered_set<std::string> features
)
  : feature_manager_(std::move(features)) {
  AddRuntimePartsNoUpdate({core});
  const auto& core_part = runtime_parts_.at(core.id);
  runtime_ = std::make_shared<Runtime>(
    core_part.data_reader, core_part.available_versions.at(CORE_VERSION_NAME).config_path, std::move(environments)
  );

  if (runtime_ == nullptr) {
    throw std::runtime_error("Failed to create runtime");
  }

  AddRuntimePartsNoUpdate(parts);
  UpdateState(/*free_unused=*/false);
};

UpdateAction RuntimeManager::InitialUpdateState() {
  return UpdateState();
}

std::shared_ptr<Runtime> RuntimeManager::Model() const {
  return runtime_;
}

std::shared_ptr<Runtime> RuntimeManager::CreateRuntime(const std::string& version_id) const {
  const auto version_to_part_it = version_to_part_.find(version_id);
  if (version_to_part_it == version_to_part_.end()) {
    throw std::invalid_argument("No part contains runtime version '" + version_id + "'");
  }

  const auto& part = runtime_parts_.at(version_to_part_it->second);
  const auto& version = part.available_versions.at(version_id);
  auto model = std::make_shared<Runtime>(part.data_reader, version.config_path, EnvironmentsMap{});

  std::vector<SubmodelVersionChain> active_versions;
  std::unordered_map<SubmodelVersionChain, SubmodelVersionState> active_current_state;
  SubmodelGraphBuilder builder(*this, *model, nullptr, nullptr);

  std::vector<SubmodelVersionChain> required_versions;
  model->Core()->GatherRequiredVersions(required_versions, feature_manager_.GetFeatures());
  for (const auto& required_version : required_versions) {
    auto version_chain = required_version;
    const auto required_submodel_version = version_chain.ids_chain.back().version;
    version_chain.ids_chain.back().version.clear();
    if (!builder.Add(
          active_versions,
          active_current_state,
          {
            .version_chain = std::move(version_chain),
            .version_from = "",
            .version_to = {
              .current_version = required_submodel_version,
              .required_version = required_submodel_version,
            },
          }
        )) {
      throw std::runtime_error("Failed to create runtime from version '" + version_id + "'");
    }
  }
  model->SetActiveSubmodelVersions(active_versions);
  return model;
}

const std::unordered_set<std::string>& RuntimeManager::Features() const {
  return feature_manager_.GetFeatures(FeatureSetType::CLIENT);
}

UpdateAction RuntimeManager::SetFeatures(const std::unordered_set<std::string>& features) {
  return feature_manager_.SetFeatures(*this, features, FeatureSetType::CLIENT);
}

UpdateAction RuntimeManager::AddFeature(const std::string& feature) {
  return feature_manager_.AddFeature(*this, feature, FeatureSetType::CLIENT);
}

UpdateAction RuntimeManager::RemoveFeature(const std::string& feature) {
  return feature_manager_.RemoveFeature(*this, feature, FeatureSetType::CLIENT);
}

void RuntimeManager::AddRuntimePartsNoUpdate(const std::vector<RuntimePart>& parts) {
  for (const auto& part : parts) {
    runtime_parts_.emplace(
      part.id,
      RuntimePartStatus{
        .data_reader = CreateLazyCachingDataReader(
          [data = part.data]() {
            return std::visit(
              overloaded{
                [](const ModelFileData& model_data) {
                  return CreateDataReader(model_data.path, model_data.reader_options);
                },
                [](const ModelPersistentTarData& model_data) {
                  return CreatePersistentTarBufferDataReader(
                    Create<IDataSpanPtr>(model_data.data, model_data.size), model_data.reader_options
                  );
                },
                [](const ModelTemporaryTarData& model_data) -> IDataReaderPtr {
                  return CreateHoldingTarBufferDataReader(model_data.data_provider(), model_data.reader_options);
                }
              },
              data
            );
          },
          std::visit(
            [](const auto& model_data) {
              return model_data.reader_options;
            },
            part.data
          )
        ),
        .available_versions = part.versions,
        .permanent = part.permanent,
        .deferred = part.deferred,
      }
    );

    for (const auto& [version_id, _] : part.versions) {
      version_to_part_[version_id] = part.id;
    }
  }
}

UpdateAction RuntimeManager::AddRuntimeParts(const std::vector<RuntimePart>& parts) {
  AddRuntimePartsNoUpdate(parts);
  return UpdateState();
}

UpdateAction RuntimeManager::RemoveRuntimeParts(const std::vector<std::string>& part_ids) {
  NotifyBeforePartsRemoved(part_ids);
  for (const auto& part_id : part_ids) {
    RemovePart(part_id);
  }
  return UpdateState();
}

void RuntimeManager::RemovePart(const std::string& part_id) {
  for (const auto& [version, _] : runtime_parts_.at(part_id).available_versions) {
    // Another part may already own the version (the fixed part arrived before the broken one was removed).
    if (const auto it = version_to_part_.find(version); it != version_to_part_.end() && it->second == part_id) {
      version_to_part_.erase(it);
    }
  }
  runtime_parts_.erase(part_id);
}

std::optional<std::vector<SubmodelMeta>> RuntimeManager::CollectSubmodelsMeta(
  const std::vector<SubmodelVersionChain>& roots, const SubmodelRequireMode root_require_mode
) {
  try {
    return CollectSubmodelsMeta(roots, std::optional{root_require_mode});
  } catch (const std::exception& e) {
    std::cerr << "[WARNING] Failed to collect submodel metadata: " << e.what() << std::endl;
    return std::nullopt;
  }
}

UpdateAction RuntimeManager::SetState(
  const std::vector<RuntimePart>& parts, const std::unordered_set<std::string>& features
) {
  std::unordered_set<std::string> part_ids;
  part_ids.reserve(parts.size());

  std::vector<RuntimePart> new_parts;
  new_parts.reserve(parts.size());
  for (const auto& part : parts) {
    part_ids.insert(part.id);
    if (runtime_parts_.find(part.id) == runtime_parts_.end()) {
      new_parts.push_back(part);
    }
  }

  std::vector<std::string> removed_parts;
  removed_parts.reserve(runtime_parts_.size());
  for (const auto& [part_id, _] : runtime_parts_) {
    if (part_ids.find(part_id) == part_ids.end()) {
      removed_parts.push_back(part_id);
    }
  }

  feature_manager_.SetFeatures(features, FeatureSetType::CLIENT);
  NotifyBeforePartsRemoved(removed_parts);
  for (const auto& part_id : removed_parts) {
    RemovePart(part_id);
  }
  AddRuntimePartsNoUpdate(new_parts);
  return UpdateState();
}

std::unordered_set<std::string> RuntimeManager::GetMissingSubmodelVersions(
  const std::unordered_set<std::string>& versions
) {
  std::vector<SubmodelVersionChain> version_chains;
  version_chains.reserve(versions.size());
  for (const auto& version : versions) {
    version_chains.push_back({.ids_chain = {{.version = version}}});
  }

  std::unordered_set<std::string> missing_versions;
  for (const auto& meta : CollectSubmodelsMeta(version_chains, std::optional{SubmodelRequireMode::SELECTIVE})) {
    if (!meta.config_version.has_value()) {
      missing_versions.insert(meta.version_id);
    }
  }

  return missing_versions;
}

std::unordered_set<std::string> RuntimeManager::GetMissingSubmodelVersions(
  const std::vector<SubmodelVersionChain>& version_chains
) {
  std::unordered_set<std::string> missing_versions;
  for (const auto& meta : CollectSubmodelsMeta(version_chains, std::nullopt)) {
    if (!meta.config_version.has_value()) {
      missing_versions.insert(meta.version_id);
    }
  }

  return missing_versions;
}

UpdateAction RuntimeManager::UpdateState(bool free_unused) {
  const auto update_start = std::chrono::steady_clock::now();
  std::vector<SubmodelVersionChain> required_versions;
  runtime_->Core()->GatherRequiredVersions(required_versions, feature_manager_.GetFeatures());

  std::unordered_map<SubmodelVersionChain, SubmodelVersionState> version_changes;

  std::vector<VersionToSwitch> versions_to_switch;
  for (auto version_chain : required_versions) {
    const auto& submodel_id = version_chain.ids_chain.back().id;
    auto required_version = version_chain.ids_chain.back().version;

    version_chain.ids_chain.back().version.clear();
    auto current_version = current_state_.find(version_chain) == current_state_.end()
      ? ""
      : current_state_.at(version_chain).current_version;
    auto target_version = version_to_part_.find(required_version) == version_to_part_.end()
      ? runtime_->Core()->SubmodelVersion(version_chain.PopBack())->DefaultSubmodels().at(submodel_id).submodel_version
      : required_version;

    SubmodelVersionState target_state{.current_version = target_version, .required_version = required_version};

    if (current_version != target_version) {
      versions_to_switch.push_back(
        {.version_chain = version_chain, .version_from = current_version, .version_to = target_state}
      );
    } else if (current_state_.at(version_chain) != target_state) {
      assert(current_state_.at(version_chain).current_version == target_state.current_version);
      current_state_.at(version_chain) = target_state;
    }
  }

  for (auto it = versions_to_switch.begin(); it != versions_to_switch.end();) {
    bool to_remove = false;
    for (auto it2 = versions_to_switch.begin(); it2 != versions_to_switch.end(); ++it2) {
      if (it != it2 && it2->version_chain.IsPrefixOf(it->version_chain)) {
        to_remove = true;
        break;
      }
    }
    it = to_remove ? versions_to_switch.erase(it) : ++it;
  }

  std::vector<BuildFailure> build_failures;
  SwitchVersions(versions_to_switch, build_failures);
  // A slot that did not switch stays on its previous version but requires the new one.
  for (const auto& version : versions_to_switch) {
    if (
      const auto state = current_state_.find(version.version_chain);
      state != current_state_.end() && state->second.current_version != version.version_to.current_version
    ) {
      state->second.required_version = version.version_to.required_version;
    }
  }

  UpdateAction result;

  std::unordered_map<std::string, bool> runtime_part_in_use;
  for (const auto& [id, part] : runtime_parts_) {
    runtime_part_in_use[id] = false;
  }

  runtime_part_in_use[version_to_part_[CORE_VERSION_NAME]] = true;
  // There is no point in requesting a version that was not applied but whose part is present: the client would hand
  // back the same part, and the free and the request would go in circles, so the part stays; what the builder was
  // missing is taken from the build failures.
  const auto keep = [&](const std::string& version) {
    const auto part = version_to_part_.find(version);
    if (part != version_to_part_.end()) {
      runtime_part_in_use[part->second] = true;
    }
    return part != version_to_part_.end();
  };
  const auto keep_or_request = [&](const std::string& version) {
    if (!keep(version) && !version.empty()) {
      result.required_versions.insert(version);
    }
  };
  for (const auto& [_, state] : current_state_) {
    if (state.current_version != state.required_version) {
      keep_or_request(state.required_version);
      result.inactive_versions.insert(state.required_version);
    }
    runtime_part_in_use[version_to_part_[state.current_version]] = true;
  }
  // A parent from the failure chain that was removed from its slot: its subtree (siblings built before the failure,
  // at any depth) will be needed by the next build, so it is kept while the failure lasts, together with the versions
  // from the rules of disabled features; the subtree of a parent that stays in its slot is kept by the current state.
  std::vector<std::string> subtree_roots;
  for (const auto& failure : build_failures) {
    SubmodelVersionChain slot;
    for (const auto& id : failure.version_chain.ids_chain) {
      slot.ids_chain.push_back({.id = id.id});
      keep(id.version);
      if (&id == &failure.version_chain.ids_chain.back()) {
        break;
      }
      if (
        const auto state = current_state_.find(slot);
        state == current_state_.end() || state->second.current_version != id.version
      ) {
        subtree_roots.push_back(id.version);
      }
      slot.ids_chain.back().version = id.version;
    }
    // The required version has no part, so the fallback was being built instead: keep the fallback's part and request
    // the required version.
    keep_or_request(failure.required_version);

    // A deferred part may still be completed: request its version again.
    const auto& version = failure.version_chain.ids_chain.back().version;
    result.inactive_versions.insert(version);
    const auto part = version_to_part_.find(version);
    if (part != version_to_part_.end() && runtime_parts_.at(part->second).deferred) {
      result.required_versions.insert(version);
    }
  }
  for (std::unordered_set<std::string> seen; !subtree_roots.empty();) {
    const auto version = std::move(subtree_roots.back());
    subtree_roots.pop_back();
    if (!seen.insert(version).second || !keep(version)) {
      continue;
    }
    try {
      for (const auto& submodel : GetSubmodelProperties(version).usable_submodels) {
        subtree_roots.push_back(submodel.submodel_version);
      }
    } catch (const std::exception& error) {
      // The config cannot be read: the part itself is kept, there is nothing deeper.
      std::cerr << "[WARNING] Cannot read nested versions of \"" << version << "\": " << error.what() << '\n';
    }
  }

  std::vector<SubmodelVersionChain> active_versions;
  active_versions.reserve(current_state_.size() + 1);
  active_versions.push_back({.ids_chain = {{.version = CORE_VERSION_NAME}}});
  for (const auto& [_, state] : current_state_) {
    active_versions.push_back({.ids_chain = {{.version = state.current_version}}});
  }

  for (const auto& meta : CollectSubmodelsMeta(active_versions, std::optional{SubmodelRequireMode::SELECTIVE})) {
    if (!meta.config_version.has_value()) {
      result.required_versions.insert(meta.version_id);
    } else {
      runtime_part_in_use[version_to_part_[meta.version_id]] = true;
    }
  }

  if (free_unused) {
    std::vector<std::string> parts_to_free;
    for (const auto& [part_id, in_use] : runtime_part_in_use) {
      if (!in_use && !runtime_parts_.at(part_id).permanent && !runtime_parts_.at(part_id).deferred) {
        parts_to_free.push_back(part_id);
      }
    }
    NotifyBeforePartsRemoved(parts_to_free);
    for (const auto& part_id : parts_to_free) {
      RemovePart(part_id);
      result.freed_parts.insert(part_id);
    }
  }

  result.consumed_versions = runtime_->CollectConsumedVersions();
  result.available_features = CollectAvailableFeatures();

  for (const auto& [_, part] : runtime_parts_) {
    part.data_reader->Free();
  }

  NotifyAfterStateUpdated(result);
  const std::chrono::duration<double, std::milli> update_ms = std::chrono::steady_clock::now() - update_start;
  std::cerr << "[load] update_state ms=" << static_cast<long>(update_ms.count()) << '\n';
  return result;
}

void RuntimeManager::NotifyBeforePartsRemoved(const std::vector<std::string>& part_ids) {
  if (part_ids.empty()) {
    return;
  }

  const auto capabilities = GetCapabilities();
  for (const auto& capability : capabilities) {
    capability->OnBeforePartsRemoved(part_ids);
  }
}

void RuntimeManager::NotifyAfterStateUpdated(const UpdateAction& action) {
  const auto capabilities = GetCapabilities();
  for (const auto& capability : capabilities) {
    capability->OnAfterStateUpdated(action);
  }
}

std::vector<std::shared_ptr<RuntimeCapabilityBase>> RuntimeManager::GetCapabilities() const {
  std::vector<std::shared_ptr<RuntimeCapabilityBase>> result;
  std::lock_guard<std::mutex> lock(capabilities_mutex_);
  result.reserve(capabilities_.size());
  for (const auto& [_, capability] : capabilities_) {
    result.push_back(capability);
  }
  return result;
}

bool RuntimeManager::AddSubmodelVersion(
  std::vector<SubmodelVersionChain>& active_versions,
  std::unordered_map<SubmodelVersionChain, SubmodelVersionState>& active_current_state,
  std::vector<BuildFailure>& failures,
  const VersionToSwitch& version_to_switch
) {
  SubmodelGraphBuilder builder(*this, *runtime_, &current_state_, &failures);
  return builder.Add(active_versions, active_current_state, version_to_switch);
}

void RuntimeManager::SetActiveSubmodelVersions(
  const std::vector<SubmodelVersionChain>& active_versions,
  const std::unordered_map<SubmodelVersionChain, SubmodelVersionState>& active_current_state
) {
  for (const auto& current_state : active_current_state) {
    current_state_[current_state.first] = current_state.second;
  }

  runtime_->SetActiveSubmodelVersions(active_versions);
}

void RuntimeManager::RemoveSubmodelVersion(const SubmodelVersionChain& version_to_delete) {
  runtime_->RemoveSubmodelVersion(version_to_delete);
  for (auto it = current_state_.begin(); it != current_state_.end();
       it = version_to_delete.IsPrefixOf(it->first) ? current_state_.erase(it) : ++it);
}

void RuntimeManager::SwitchVersions(
  const std::vector<VersionToSwitch>& versions_to_switch, std::vector<BuildFailure>& failures
) {
  std::vector<VersionToSwitch> valid_versions_to_switch;
  std::vector<SubmodelVersionChain> active_versions;
  std::unordered_map<SubmodelVersionChain, SubmodelVersionState> active_current_state;

  // The branches are independent: build them at once; on a failure remove the built ones, otherwise the next build
  // would trip over them.
  struct Built {
    std::vector<SubmodelVersionChain> active_versions;
    std::unordered_map<SubmodelVersionChain, SubmodelVersionState> active_current_state;
    std::vector<BuildFailure> failures;
    bool added = false;
  };
  std::vector<Built> built(versions_to_switch.size());
  const auto remove_built = [&] {
    for (const auto& branch : built) {
      if (branch.added) {
        RemoveSubmodelVersion(branch.active_versions.front());
      }
    }
  };
  try {
    RunInParallel(versions_to_switch.size(), static_cast<std::size_t>(runtime_->LoadThreads()), [&](std::size_t i) {
      built[i].added = AddSubmodelVersion(
        built[i].active_versions, built[i].active_current_state, built[i].failures, versions_to_switch[i]
      );
      return built[i].added || !versions_to_switch[i].version_from.empty();
    });
  } catch (...) {
    remove_built();
    throw;
  }
  for (std::size_t i = 0; i < versions_to_switch.size(); ++i) {
    failures.insert(failures.end(), built[i].failures.begin(), built[i].failures.end());
    if (built[i].added) {
      valid_versions_to_switch.push_back(versions_to_switch[i]);
      active_versions.insert(active_versions.end(), built[i].active_versions.begin(), built[i].active_versions.end());
      for (auto& [chain, state] : built[i].active_current_state) {
        active_current_state[chain] = std::move(state);
      }
    } else if (versions_to_switch[i].version_from.empty()) {
      remove_built();
      throw std::runtime_error("Failure on switch to new submodel version");
    }
  }

  SetActiveSubmodelVersions(active_versions, active_current_state);

  for (const auto& version_to_switch : valid_versions_to_switch) {
    if (version_to_switch.version_from.empty()) {
      continue;
    }
    SubmodelVersionChain old_version_chain = version_to_switch.version_chain;
    old_version_chain.ids_chain.back().version = version_to_switch.version_from;
    RemoveSubmodelVersion(old_version_chain);
  }
}

std::vector<std::string> RuntimeManager::GetPartIds() const {
  std::vector<std::string> part_ids;
  part_ids.reserve(runtime_parts_.size());
  for (const auto& [part_id, _] : runtime_parts_) {
    part_ids.push_back(part_id);
  }
  return part_ids;
}

bool RuntimeManager::HasPart(const std::string& part_id) const {
  return runtime_parts_.find(part_id) != runtime_parts_.end();
}

bool RuntimeManager::IsPartReady(const std::string& part_id) {
  const auto part = runtime_parts_.find(part_id);
  if (part == runtime_parts_.end()) {
    return false;
  }
  if (!part->second.deferred) {
    return true;
  }
  try {
    for (const auto& [version_id, _] : part->second.available_versions) {
      GetSubmodelProperties(version_id);
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

std::optional<std::string> RuntimeManager::GetVersionPart(const std::string& version_id) const {
  const auto part = version_to_part_.find(version_id);
  if (part == version_to_part_.end()) {
    return std::nullopt;
  }
  return part->second;
}

ILazyDataReaderPtr RuntimeManager::GetDataReader(const std::string& part_id) {
  return runtime_parts_.at(part_id).data_reader;
}

FeatureManager& RuntimeManager::GetFeatureManager() {
  return feature_manager_;
}

AvailableFeatures RuntimeManager::CollectAvailableFeatures() {
  AvailableFeatures available_features;

  auto add_version_features = [this, &available_features](const std::string& version_id) {
    const auto& properties = GetSubmodelProperties(version_id);
    available_features.insert(properties.feature_ids.begin(), properties.feature_ids.end());
  };

  add_version_features(CORE_VERSION_NAME);
  for (const auto& [_, state] : current_state_) {
    add_version_features(state.current_version);
  }

  return available_features;
}

std::vector<SubmodelMeta> RuntimeManager::CollectSubmodelsMeta(
  const std::vector<SubmodelVersionChain>& roots, const std::optional<SubmodelRequireMode> root_require_mode
) {
  std::unordered_set<std::string> collected_versions;
  SubmodelRoots submodel_roots;
  submodel_roots.reserve(roots.size());

  for (const auto& version_chain : roots) {
    for (const auto& version_id : version_chain.ids_chain) {
      collected_versions.insert(version_id.version);
    }

    if (version_chain.ids_chain.empty()) {
      submodel_roots.push_back({
        .submodel_version = CORE_VERSION_NAME,
        .require_mode = root_require_mode.value_or(SubmodelRequireMode::SELECTIVE),
      });
      continue;
    }

    const auto require_mode = root_require_mode.value_or(GetSubmodelRequireMode(version_chain));
    submodel_roots.push_back({
      .submodel_version = version_chain.ids_chain.back().version,
      .require_mode = require_mode,
    });
  }

  return CollectSubmodelsMeta(submodel_roots, std::move(collected_versions));
}

std::vector<SubmodelMeta> RuntimeManager::CollectSubmodelsMeta(
  const SubmodelRoots& roots, std::unordered_set<std::string> collected_versions
) {
  std::deque<std::pair<std::string, SubmodelRequireMode>> queue;

  auto enqueue = [&](const std::string& target, SubmodelRequireMode mode) {
    if (mode == SubmodelRequireMode::NONE) {
      return;
    }
    collected_versions.insert(target);

    if (version_to_part_.count(target)) {
      queue.emplace_back(target, mode);
    }
  };

  for (const auto& root : roots) {
    enqueue(root.submodel_version, root.require_mode);
  }

  while (!queue.empty()) {
    auto [version_id, require_mode] = std::move(queue.front());
    queue.pop_front();

    const auto* properties = TryGetSubmodelProperties(version_id);
    if (properties == nullptr) {
      continue;
    }
    const auto& usable_submodels = properties->usable_submodels;
    switch (require_mode) {
      case SubmodelRequireMode::RECURSIVE:
        for (const auto& submodel : usable_submodels) {
          enqueue(submodel.submodel_version, SubmodelRequireMode::RECURSIVE);
        }
        break;
      case SubmodelRequireMode::SELECTIVE:
        for (const auto& submodel : usable_submodels) {
          enqueue(submodel.submodel_version, submodel.require_mode);
        }
        break;
      case SubmodelRequireMode::IMMEDIATE:
        break;
      case SubmodelRequireMode::NONE:
        assert(false);
        break;
    }
  }

  std::vector<SubmodelMeta> result;
  result.reserve(collected_versions.size());
  for (const auto& version_id : collected_versions) {
    SubmodelMeta meta{.version_id = version_id};
    if (version_to_part_.count(version_id)) {
      if (const auto* properties = TryGetSubmodelProperties(version_id)) {
        meta.config_version = properties->config_version;
      }
    }
    result.push_back(std::move(meta));
  }
  return result;
}

SubmodelRequireMode RuntimeManager::GetSubmodelRequireMode(const SubmodelVersionChain& version_chain) {
  if (version_chain.ids_chain.size() < 2) {
    return SubmodelRequireMode::SELECTIVE;
  }

  const auto& parent_version = version_chain.ids_chain[version_chain.ids_chain.size() - 2].version;
  if (version_to_part_.find(parent_version) == version_to_part_.end()) {
    return SubmodelRequireMode::SELECTIVE;
  }

  const auto& child = version_chain.ids_chain.back();
  for (const auto& submodel : GetSubmodelProperties(parent_version).usable_submodels) {
    if (submodel.submodel_id == child.id && submodel.submodel_version == child.version) {
      return submodel.require_mode;
    }
  }

  throw std::runtime_error(
    "Cannot find submodel edge from parent version '" + parent_version + "' to child '" + child.id + "#" + child.version
    + "' for version chain '" + version_chain.AsString() + "'"
  );
}

const RuntimeManager::SubmodelProperties* RuntimeManager::TryGetSubmodelProperties(const std::string& version_id) {
  if (!runtime_parts_.at(version_to_part_.at(version_id)).deferred) {
    return &GetSubmodelProperties(version_id);
  }
  try {
    return &GetSubmodelProperties(version_id);
  } catch (const std::exception& e) {
    std::cerr << "[WARNING] Deferred submodel version \"" << version_id << "\" is not readable yet: " << e.what()
              << std::endl;
    return nullptr;
  }
}

const RuntimeManager::SubmodelProperties& RuntimeManager::GetSubmodelProperties(const std::string& version_id) {
  auto& part = runtime_parts_.at(version_to_part_.at(version_id));
  if (auto it = part.submodel_properties.find(version_id); it != part.submodel_properties.end()) {
    return it->second;
  }

  const auto& config_path = part.available_versions.at(version_id).config_path;

  auto span = part.data_reader->ReadTransientData(config_path);
  auto [bytes, size] = EnsureNotNull(span, config_path)->Get();
  auto params = ParseConfig<SubmodelParams>(ParseYaml(bytes, size));

  SubmodelProperties properties{
    .config_version = params.version,
  };
  for (const auto& [submodel_id, default_submodel_version] : params.default_submodels) {
    properties.usable_submodels.push_back({
      .submodel_id = submodel_id,
      .submodel_version = default_submodel_version.submodel_version,
      .require_mode = default_submodel_version.require_mode,
    });
  }
  for (const auto& feature : params.features) {
    properties.usable_submodels.push_back({
      .submodel_id = feature.submodel_id,
      .submodel_version = feature.submodel_version,
      .require_mode = feature.require_mode,
    });
    properties.feature_ids.insert(feature.feature_ids.begin(), feature.feature_ids.end());
  }

  return part.submodel_properties.emplace(version_id, std::move(properties)).first->second;
}

} // namespace giga_edge
