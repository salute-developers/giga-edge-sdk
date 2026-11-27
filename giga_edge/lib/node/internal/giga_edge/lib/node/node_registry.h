#pragma once

#include "node.h"

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/node_params.h"

#include <functional>
#include <optional>

// Node docs serve only the README generator; release client libraries leave them out.
#ifdef GIGA_EDGE_STRIP_NODE_DOCS
#define GIGA_EDGE_NODE_DOC(text) ""
#else
#define GIGA_EDGE_NODE_DOC(text) text
#endif

#define DEPRECATE_NODE(node_id, deprecation_reason)                                                                    \
  inline const bool node_id##_##deprecated = NodeRegistry::DeprecateNode(#node_id, deprecation_reason);

#define REGISTER_DYNAMIC_INPUTS(node_id)                                                                               \
  static inline const bool node_id##_inputs_deprecated = NodeRegistry::RegisterDynamicInputs(#node_id);                \
public:                                                                                                                \
  std::unordered_set<std::string> GetDynamicInputs() override;

#define REGISTER_DYNAMIC_OUTPUTS(node_id)                                                                              \
  static inline const bool node_id##_outputs_deprecated = NodeRegistry::RegisterDynamicOutputs(#node_id);              \
public:                                                                                                                \
  std::unordered_set<std::string> GetDynamicOutputs() override;

#define REGISTER_INPUT(node_id, input_name, getter_name, input_type, description)                                      \
private:                                                                                                               \
  const input_type& Get##getter_name() const {                                                                         \
    return inputs_data_->GetSingle<input_type>(#input_name);                                                           \
  }                                                                                                                    \
                                                                                                                       \
  static inline const bool node_id##_##input_name##_input_registered = NodeRegistry::RegisterNodeInput(                \
    #node_id, #input_name, #input_type, GIGA_EDGE_NODE_DOC(description), false, false                                  \
  );

#define REGISTER_OPTIONAL_INPUT(node_id, input_name, getter_name, input_type, description)                             \
private:                                                                                                               \
  const std::optional<std::reference_wrapper<const input_type>> Get##getter_name() const {                             \
    if (inputs_data_->Contains(#input_name)) {                                                                         \
      return inputs_data_->GetSingle<input_type>(#input_name);                                                         \
    } else {                                                                                                           \
      return std::nullopt;                                                                                             \
    }                                                                                                                  \
  }                                                                                                                    \
                                                                                                                       \
  static inline const bool node_id##_##input_name##_input_registered = NodeRegistry::RegisterNodeInput(                \
    #node_id, #input_name, #input_type, GIGA_EDGE_NODE_DOC(description), false, true                                   \
  );

#define REGISTER_BATCH_INPUT(node_id, input_name, getter_name, input_type, description)                                \
private:                                                                                                               \
  std::vector<const input_type*> Get##getter_name##Batch() const {                                                     \
    return inputs_data_->GetBatch<input_type>(#input_name);                                                            \
  }                                                                                                                    \
                                                                                                                       \
public:                                                                                                                \
  static inline const bool node_id##_##input_name##_input_registered = NodeRegistry::RegisterNodeInput(                \
    #node_id, #input_name, #input_type, GIGA_EDGE_NODE_DOC(description), true, false                                   \
  );

#define REGISTER_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, is_batch, is_recursive)      \
private:                                                                                                               \
  void Push##setter_name(output_type value) const {                                                                    \
    outputs_data_->Add(#output_name, std::move(value));                                                                \
  }                                                                                                                    \
  static inline const bool node_id##_##output_name##_output_registered = NodeRegistry::RegisterNodeOutput(             \
    #node_id, #output_name, #output_type, GIGA_EDGE_NODE_DOC(description), is_batch, is_recursive                      \
  );

#define REGISTER_OUTPUT(node_id, output_name, setter_name, output_type, description)                                   \
  REGISTER_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, false, false)

#define REGISTER_BATCH_OUTPUT(node_id, output_name, setter_name, output_type, description)                             \
  REGISTER_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, true, false)

#define REGISTER_RECURSIVE_OUTPUT(node_id, output_name, setter_name, output_type, description)                         \
  REGISTER_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, false, true)

#define REGISTER_OPTIONAL_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, is_batch)           \
private:                                                                                                               \
  void Push##setter_name(output_type value) const {                                                                    \
    if (outputs_data_->Has(#output_name)) {                                                                            \
      outputs_data_->Add(#output_name, std::move(value));                                                              \
    }                                                                                                                  \
  }                                                                                                                    \
  bool Has##setter_name##Output() const {                                                                              \
    return outputs_data_->Has(#output_name);                                                                           \
  }                                                                                                                    \
  static inline const bool node_id##_##output_name##_output_registered = NodeRegistry::RegisterNodeOutput(             \
    #node_id, #output_name, #output_type, GIGA_EDGE_NODE_DOC(description), is_batch, false, true                       \
  );

#define REGISTER_OPTIONAL_OUTPUT(node_id, output_name, setter_name, output_type, description)                          \
  REGISTER_OPTIONAL_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, false)

#define REGISTER_OPTIONAL_BATCH_OUTPUT(node_id, output_name, setter_name, output_type, description)                    \
  REGISTER_OPTIONAL_OUTPUT_IMPL(node_id, output_name, setter_name, output_type, description, true)

#define NODE_CREATOR_REGISTER_BEGIN(node_name, node_type)                                                              \
  inline const bool node_name ## _creator_registered = NodeRegistry::RegisterNodeCreator( \
    #node_name, \
    []( \
      const NodeBaseConfig& base_config, \
      [[maybe_unused]] const IDataReaderPtr& data_reader, \
      [[maybe_unused]] const YAML::Node& params \
    ) { \
      return std::make_shared<node_type>( \
        base_config

#define NODE_CREATOR_ARG_DATA_READER , data_reader

#define NODE_CREATOR_ARG_PARAMS(node_params_type) , ParseConfig<node_params_type>(params)

#define NODE_CREATOR_REGISTER_END                                                                                      \
       );                                                                                                              \
  }                                                                                                                    \
  );

#define NODE_INFO_REGISTER(node_name, description)                                                                     \
  inline const bool node_name##_info_registered = NodeRegistry::RegisterInfo(                                          \
    #node_name, GIGA_EDGE_NODE_DOC(description), GIGA_EDGE_NODE_DOC(__FILE__), __LINE__                                \
  );

#define REGISTER_NODE_PARAMS_true_DATA_true(node_name, node_type, node_params_type, description)                       \
  NODE_CREATOR_REGISTER_BEGIN(node_name, node_type)                                                                    \
  NODE_CREATOR_ARG_DATA_READER                                                                                         \
  NODE_CREATOR_ARG_PARAMS(node_params_type)                                                                            \
  NODE_CREATOR_REGISTER_END                                                                                            \
  NODE_INFO_REGISTER(node_name, description)

#define REGISTER_NODE_PARAMS_true_DATA_false(node_name, node_type, node_params_type, description)                      \
  NODE_CREATOR_REGISTER_BEGIN(node_name, node_type)                                                                    \
  NODE_CREATOR_ARG_PARAMS(node_params_type)                                                                            \
  NODE_CREATOR_REGISTER_END                                                                                            \
  NODE_INFO_REGISTER(node_name, description)

#define REGISTER_NODE_PARAMS_false_DATA_true(node_name, node_type, node_params_type, description)                      \
  NODE_CREATOR_REGISTER_BEGIN(node_name, node_type)                                                                    \
  NODE_CREATOR_ARG_DATA_READER                                                                                         \
  NODE_CREATOR_REGISTER_END                                                                                            \
  NODE_INFO_REGISTER(node_name, description)

#define REGISTER_NODE_PARAMS_false_DATA_false(node_name, node_type, node_params_type, description)                     \
  NODE_CREATOR_REGISTER_BEGIN(node_name, node_type)                                                                    \
  NODE_CREATOR_REGISTER_END                                                                                            \
  NODE_INFO_REGISTER(node_name, description)

#define REGISTER_NODE_IMPL(node_name, node_type, has_params, node_params_type, need_data, description)                 \
  REGISTER_NODE_PARAMS_##has_params##_DATA_##need_data(node_name, node_type, node_params_type, description)

#define REGISTER_NODE_SIMPLE(node_name, node_type, description)                                                        \
  REGISTER_NODE_IMPL(node_name, node_type, false, 0, false, description)
#define REGISTER_NODE_PARAMS(node_name, node_type, node_params_type, description)                                      \
  REGISTER_NODE_IMPL(node_name, node_type, true, node_params_type, false, description)
#define REGISTER_NODE_PARAMS_AND_DATA(node_name, node_type, node_params_type, need_data, description)                  \
  REGISTER_NODE_IMPL(node_name, node_type, true, node_params_type, need_data, description)

#define GET_6TH(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6

#define REGISTER_NODE_MACRO_CHOOSER(...)                                                                               \
  GET_6TH(__VA_ARGS__, REGISTER_NODE_PARAMS_AND_DATA, REGISTER_NODE_PARAMS, REGISTER_NODE_SIMPLE)

#define REGISTER_NODE(...) REGISTER_NODE_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

namespace giga_edge {

class NodeRegistry {
public:
  using NodeCreatorFunction = std::function<
    std::shared_ptr<NodeBase>(const NodeBaseConfig& base_config, const IDataReaderPtr&, const YAML::Node&)>;

  struct NodeOutputInfo {
    std::string type;
    std::string description;
    bool is_batch;
    bool is_recursive; // supported only for outputs
    bool is_optional;
  };

  struct NodeInfo {
    NodeCreatorFunction node_creator_function;

    std::unordered_map<std::string, NodeOutputInfo> inputs;
    bool has_dynamic_inputs = false;
    std::unordered_map<std::string, NodeOutputInfo> outputs;
    bool has_dynamic_outputs = false;

    std::string filepath;
    std::string description;

    bool deprecated = false;
    std::string deprecation_reason;
  };

  using NodeInfoMap = std::unordered_map<std::string, NodeInfo>;

  static bool RegisterNodeCreator(const std::string& id, NodeCreatorFunction f);

  static bool RegisterNodeInput(
    const std::string& node_id,
    const std::string& input_id,
    const std::string& type,
    const std::string& description,
    bool is_batch,
    bool is_optional
  );
  static bool RegisterNodeOutput(
    const std::string& node_id,
    const std::string& input_id,
    const std::string& type,
    const std::string& description,
    bool is_batch,
    bool is_recursive,
    bool is_optional = false
  );
  static bool RegisterInfo(
    const std::string& node_id, const std::string& description, const std::string& filepath, int32_t line
  );
  static bool RegisterDynamicInputs(const std::string& node_id);
  static bool RegisterDynamicOutputs(const std::string& node_id);

  static bool DeprecateNode(const std::string& node_id, const std::string& reason);

  static std::shared_ptr<NodeBase> CreateNode(
    const std::string& id,
    const NodeBaseConfig& base_config,
    const IDataReaderPtr& data_reader,
    const YAML::Node& node_params
  );

  static NodeInfoMap& GetNodeInfoMap();
};

} // namespace giga_edge
