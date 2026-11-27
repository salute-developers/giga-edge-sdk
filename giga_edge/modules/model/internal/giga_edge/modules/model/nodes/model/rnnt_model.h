#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/model/models/model.h"
#include "giga_edge/modules/model/nodes/model/rnnt_model_params.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class CallLimiter {
public:
  CallLimiter(int n_frames, int limit);
  void Reset();
  void NewFrame();
  bool TryCall();

private:
  std::vector<int> n_calls_;
  int shift_;
  int sum_n_calls_;
  int limit_;
};

class RNNTModelNode : public NodeBase {
public:
  RNNTModelNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const RNNTModelNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_OPTIONAL_INPUT(rnnt_model, need_reset, NeedReset, bool, "Whether to reset model before process");
  REGISTER_OPTIONAL_INPUT(
    rnnt_model,
    encoded_len,
    EncodedLen,
    std::vector<float>,
    "Number of valid encoder frames in this input (one value, e.g. the encoder's encoded_len); frames past it "
    "are padding and are skipped. Empty means all frames are valid"
  );

  REGISTER_DYNAMIC_INPUTS(rnnt_model);
  REGISTER_DYNAMIC_OUTPUTS(rnnt_model);

  REGISTER_RECURSIVE_OUTPUT(rnnt_model, reset_encoder_model, ResetEncoderModel, bool, "Whether to reset encoder model");

private:
  void DoInitRecursiveOutputs() override;

  void ResetDecoder();
  void ResetCalls();

private:
  void InitDecoder(const IDataReaderPtr& data_reader, const NodeBaseConfig& base_config);
  void InitJoint(const IDataReaderPtr& data_reader, const NodeBaseConfig& base_config);
  void InitJointData(const std::vector<std::vector<float>>& encoded_features);

  std::vector<std::vector<std::vector<float>>> GetEncodedFeatures();
  void CallDecoder();

  struct JointStep {
    bool is_blank = false;
    bool is_eos = false;
    std::vector<std::vector<int>> decoder_input;
    std::unordered_map<std::string, std::vector<float>> outputs;
  };
  JointStep ReadJointStep() const;
  JointStep ReadTokensJointStep() const;
  JointStep ReadLogitsJointStep() const;

private:
  RNNTModelNodeParams params_;

  std::unique_ptr<Model> decoder_;
  int decoder_inputs_count_;

  std::vector<std::vector<float>> start_joint_decoder_input_;
  std::vector<Model::StateTensor> start_decoder_state_;

  std::vector<std::vector<int>> decoder_input_;

  std::unique_ptr<Model> joint_;

  int joint_encoder_inputs_count_;
  std::vector<std::string> joint_encoder_input_names_;
  int joint_outputs_count_;
  std::vector<std::string> joint_output_names_;

  std::vector<int> encoded_features_per_frame_;

  CallLimiter call_limiter_;
  int consequent_blanks_;
};

REGISTER_NODE(
  rnnt_model,
  RNNTModelNode,
  RNNTModelNodeParams,
  true,
  "Apply asr streaming rnnt model. Consists of decoder and joint models.\n"
  "Joint model format is:\n"
  "  Inputs: {encoder_outputs, decoder_outputs}\n"
  "  Outputs (format: tokens): {is_blank, is_eos, decoder_inputs, node_outputs}\n"
  "  Outputs (format: logits): {logits} — greedy argmax in the node, `blank_id`, `blank_penalty`, "
  "`max_symbols_per_step`\n"
  "Per-frame symbol limit is explicit: `joint.max_symbols_per_step` or `decoder.call_limiter` must be set.\n"
);

} // namespace giga_edge