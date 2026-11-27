#include "giga_edge/modules/model/nodes/model/rnnt_model.h"

#include "giga_edge/modules/model/models/model_params.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace giga_edge {

namespace {

std::vector<std::vector<int>> GenerateStartDecoderInput(
  const std::vector<std::string>& input_names, const std::vector<int>& input_values
) {
  std::vector<std::vector<int>> result;
  if (input_names.size() != input_values.size()) {
    throw std::runtime_error(
      "Invalid number of start decoder inputs, expected: " + std::to_string(input_names.size())
      + ", but found: " + std::to_string(input_values.size())
    );
  }
  result.reserve(input_names.size());
  for (size_t i = 0; i < input_names.size(); ++i) {
    result.push_back({input_values[i]});
  }
  return result;
}

} // namespace

CallLimiter::CallLimiter(int n_frames, int limit)
  : n_calls_(n_frames > 0 ? n_frames : 1, 0)
  , shift_(0)
  , sum_n_calls_(0)
  , limit_(limit) {
  if (n_frames <= 0 || limit <= 0) {
    throw std::invalid_argument("rnnt_model: call limiter values must be positive");
  }
}

void CallLimiter::Reset() {
  n_calls_.assign(n_calls_.size(), 0);
  sum_n_calls_ = 0;
  shift_ = 0;
}

void CallLimiter::NewFrame() {
  sum_n_calls_ -= n_calls_[shift_];
  if (++shift_ == static_cast<int>(n_calls_.size())) {
    shift_ = 0;
  }
  n_calls_[shift_] = 0;
}

bool CallLimiter::TryCall() {
  if (sum_n_calls_ >= limit_) {
    return false;
  }
  ++sum_n_calls_;
  ++n_calls_[shift_];
  return true;
}

RNNTModelNode::RNNTModelNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const RNNTModelNodeParams& params
)
  : NodeBase(base_config)
  , params_(params)
  , call_limiter_(params_.decoder.call_limiter.n_frames, params_.decoder.call_limiter.limit) {
  InitDecoder(data_reader, base_config);
  InitJoint(data_reader, base_config);
  ResetDecoder();
}

void RNNTModelNode::InitDecoder(const IDataReaderPtr& data_reader, const NodeBaseConfig& base_config) {
  if (params_.decoder.inputs_count == -1) {
    params_.decoder.inputs_count = params_.decoder.start_input.size();
  }
  decoder_ = std::make_unique<Model>(params_.decoder, params_.decoder.path, data_reader, base_config.environments);

  decoder_inputs_count_ = params_.decoder.start_input.size();
  auto start_decoder_input = GenerateStartDecoderInput(decoder_->GetInputNames(), params_.decoder.start_input);

  decoder_->SetInputData(start_decoder_input);
  decoder_->Invoke();

  start_decoder_state_ = decoder_->GetState();
  start_joint_decoder_input_ = decoder_->GetOutputData<float>(0);
}

void RNNTModelNode::InitJoint(const IDataReaderPtr& data_reader, const NodeBaseConfig& base_config) {
  joint_ = std::make_unique<Model>(params_.joint, params_.joint.path, data_reader, base_config.environments);

  joint_encoder_inputs_count_ = static_cast<int>(joint_->GetInputNames().size()) - decoder_->GetOutputNames().size()
    - params_.joint.const_inputs.size();
  if (joint_encoder_inputs_count_ <= 0) {
    throw std::invalid_argument("rnnt_model: joint requires at least one encoder input");
  }
  joint_encoder_input_names_ = joint_->GetInputNames();
  joint_encoder_input_names_.resize(joint_encoder_inputs_count_);

  auto joint_output_names = joint_->GetOutputNames();
  if (params_.joint.format == RNNTModelNodeParams::Joint::Format::LOGITS) {
    if (joint_output_names.size() != 1) {
      throw std::runtime_error(
        "rnnt_model: joint in logits format must have exactly one output, found "
        + std::to_string(joint_output_names.size())
      );
    }
    if (decoder_inputs_count_ != 1) {
      throw std::runtime_error("rnnt_model: joint in logits format drives a decoder with exactly one input");
    }
    joint_outputs_count_ = 1;
    joint_output_names_ = joint_output_names;
  } else {
    if (params_.joint.blank_penalty != 0) {
      throw std::invalid_argument("rnnt_model: blank_penalty needs a joint in logits format");
    }
    joint_outputs_count_ = static_cast<int>(joint_output_names.size()) - 2 - decoder_inputs_count_;
    if (joint_outputs_count_ <= 0) {
      throw std::invalid_argument("rnnt_model: token joint is missing flags, decoder inputs or predictions");
    }
    joint_output_names_ = {joint_output_names.begin() + 2 + decoder_inputs_count_, joint_output_names.end()};
  }

  encoded_features_per_frame_ = joint_->GetInputsNumElements();
  encoded_features_per_frame_.resize(joint_encoder_inputs_count_);
  if (std::any_of(encoded_features_per_frame_.begin(), encoded_features_per_frame_.end(), [](int size) {
        return size <= 0;
      })) {
    throw std::invalid_argument("rnnt_model: encoder frame dimensions must be positive");
  }
}

std::vector<std::vector<std::vector<float>>> RNNTModelNode::GetEncodedFeatures() {
  std::vector<std::vector<std::vector<float>>> encoded_features;
  for (int i = 0; i < joint_encoder_inputs_count_; ++i) {
    const std::vector<float>& data = inputs_data_->GetSingle<std::vector<float>>(joint_encoder_input_names_[i]);
    int input_size = static_cast<int>(data.size());
    if (input_size % encoded_features_per_frame_[i]) {
      throw std::runtime_error(
        "Invalid number of encoded features at input[" + std::to_string(i) + "], namely : " + std::to_string(input_size)
        + ", but expected divisible by" + std::to_string(encoded_features_per_frame_[i])
      );
    }
    int input_frames_count = input_size / encoded_features_per_frame_[i];
    if (i == 0) {
      encoded_features.resize(input_frames_count);
      for (auto& v : encoded_features) {
        v.resize(joint_encoder_inputs_count_);
      }
    } else {
      if (encoded_features.size() != input_frames_count) {
        throw std::runtime_error(
          "Receive joint encoder features input tensor with different number of frames: "
          + std::to_string(encoded_features.size()) + " and " + std::to_string(input_frames_count)
        );
      }
    }

    int num_frames = encoded_features.size();
    if (params_.joint.is_interleaved_encoder_feature_copy_strategy) {
      // Interleaved copying: distribute features across frames.
      for (int j = 0; j < data.size(); ++j) {
        encoded_features[j % num_frames][i].push_back(data[j]);
      }
    } else {
      // Sequential copying: copy features frame by frame.
      int features_per_frame = num_frames == 0 ? 0 : data.size() / num_frames;
      for (int frame = 0; frame < num_frames; ++frame) {
        auto start_it = data.begin() + frame * features_per_frame;
        auto end_it = start_it + features_per_frame;
        encoded_features[frame][i].insert(encoded_features[frame][i].end(), start_it, end_it);
      }
    }
  }

  return encoded_features;
}

void RNNTModelNode::CallDecoder() {
  decoder_->SetInputData(decoder_input_);
  decoder_->Invoke();
  auto decoded_features = decoder_->GetOutputData(0);
  joint_->SetInputData(decoded_features, joint_encoder_inputs_count_);

  decoder_input_.clear();
}

RNNTModelNode::JointStep RNNTModelNode::ReadJointStep() const {
  return params_.joint.format == RNNTModelNodeParams::Joint::Format::LOGITS
    ? ReadLogitsJointStep()
    : ReadTokensJointStep();
}

RNNTModelNode::JointStep RNNTModelNode::ReadTokensJointStep() const {
  JointStep step;
  step.is_eos = joint_->GetOutputData<int>(1, 1)[0][0];
  step.is_blank = joint_->GetOutputData<int>(0, 1)[0][0] && !step.is_eos;
  if (
    !step.is_blank
    || (params_.joint.consequent_blanks_as_eos > 0 && consequent_blanks_ + 1 >= params_.joint.consequent_blanks_as_eos)
  ) {
    step.decoder_input = joint_->GetOutputData<int>(2, decoder_inputs_count_);
    step.outputs = joint_->GetOutputData<float>(joint_output_names_);
  }
  return step;
}

RNNTModelNode::JointStep RNNTModelNode::ReadLogitsJointStep() const {
  JointStep step;
  auto logits = joint_->GetOutputData<float>(0, 1)[0];
  if (logits.empty()) {
    throw std::runtime_error("rnnt_model: joint returned no logits");
  }
  if (
    params_.joint.blank_id < -1 || params_.joint.blank_id >= static_cast<int>(logits.size())
    || params_.joint.eos_token_id < -1 || params_.joint.eos_token_id >= static_cast<int>(logits.size())
  ) {
    throw std::invalid_argument("rnnt_model: blank and EOS IDs must refer to a joint output class");
  }
  const int blank = params_.joint.blank_id >= 0 ? params_.joint.blank_id : static_cast<int>(logits.size()) - 1;
  logits[blank] -= params_.joint.blank_penalty;
  const int token = static_cast<int>(std::max_element(logits.begin(), logits.end()) - logits.begin());
  step.is_eos = params_.joint.eos_token_id >= 0 && token == params_.joint.eos_token_id;
  step.is_blank = token == blank && !step.is_eos;
  step.decoder_input = {{token}};
  if (
    !step.is_blank
    || (params_.joint.consequent_blanks_as_eos > 0 && consequent_blanks_ + 1 >= params_.joint.consequent_blanks_as_eos)
  ) {
    step.outputs[joint_output_names_[0]] = std::move(logits);
  }
  return step;
}

void RNNTModelNode::ProcessOnce() {
  auto need_reset = GetNeedReset();
  if (need_reset.has_value() && need_reset.value()) {
    Reset();
  }

  auto encoded_features = GetEncodedFeatures();
  if (const auto encoded_len = GetEncodedLen(); encoded_len.has_value() && !encoded_len.value().get().empty()) {
    const auto& lengths = encoded_len.value().get();
    const double valid = lengths.front();
    if (
      lengths.size() != 1 || !std::isfinite(valid) || valid < 0 || std::floor(valid) != valid
      || valid > static_cast<double>(encoded_features.size())
    ) {
      throw std::invalid_argument("rnnt_model: encoded_len must be a single valid encoder frame count");
    }
    encoded_features.resize(static_cast<size_t>(valid));
  }

  bool need_reset_encoder = false;
  std::unordered_map<std::string, std::vector<float>> predictions;
  for (int i = 0; i < joint_outputs_count_; ++i) {
    predictions[joint_output_names_[i]] = {};
  }

  const int max_symbols = params_.joint.max_symbols_per_step;
  for (auto& encoded_features_frame : encoded_features) {
    joint_->SetInputData(encoded_features_frame);
    call_limiter_.NewFrame();

    bool frame_done = false;
    bool found_eos_token = false;
    int emitted_in_frame = 0;
    while (true) {
      if (!decoder_input_.empty()) {
        if (!call_limiter_.TryCall()) {
          break;
        }
        CallDecoder();
      }

      joint_->Invoke();
      auto step = ReadJointStep();
      bool is_eos = step.is_eos;

      if (step.is_blank) {
        ++consequent_blanks_;
        if (!params_.joint.consequent_blanks_as_eos || consequent_blanks_ < params_.joint.consequent_blanks_as_eos) {
          frame_done = true;
          break;
        }
      } else {
        consequent_blanks_ = 0;
      }

      for (auto& [output_name, data] : step.outputs) {
        auto& target = predictions[output_name];
        target.insert(target.end(), data.begin(), data.end());
      }

      if (params_.joint.consequent_blanks_as_eos && consequent_blanks_ >= params_.joint.consequent_blanks_as_eos) {
        is_eos = true;
      }

      if (is_eos && !params_.joint.tokens_output_name.empty()) {
        auto& output = predictions.at(params_.joint.tokens_output_name);
        const auto width = step.outputs.at(params_.joint.tokens_output_name).size();
        if (
          params_.joint.eos_token_id < 0 || params_.joint.eos_token_id >= width
          || output.size() < static_cast<size_t>(width)
        ) {
          throw std::invalid_argument("rnnt_model: EOS output class is invalid");
        }
        auto begin = output.end() - width;
        std::fill(begin, output.end(), 0.0f);
        begin[params_.joint.eos_token_id] = 1.0f;
      }

      if (is_eos) {
        ResetDecoder();
        found_eos_token = true;
        need_reset_encoder = true;
        break;
      }

      decoder_input_ = std::move(step.decoder_input);

      if (max_symbols > 0 && ++emitted_in_frame >= max_symbols) {
        frame_done = true;
        break;
      }
    }
    if (!frame_done || found_eos_token) {
      break;
    }
  }

  PushResetEncoderModel(need_reset_encoder);

  for (auto& [output_name, data] : predictions) {
    outputs_data_->Add<std::vector<float>>(output_name, std::move(data));
  }
}

void RNNTModelNode::DoInitRecursiveOutputs() {
  PushResetEncoderModel(false);
}

void RNNTModelNode::Reset() {
  ResetDecoder();
  ResetCalls();
}

void RNNTModelNode::ResetDecoder() {
  decoder_->SetState(start_decoder_state_);
  joint_->Reset();
  joint_->SetInputData(start_joint_decoder_input_, joint_encoder_inputs_count_);
  decoder_input_.clear();
  consequent_blanks_ = 0;
}

void RNNTModelNode::ResetCalls() {
  call_limiter_.Reset();
}

std::unordered_set<std::string> RNNTModelNode::GetDynamicInputs() {
  return {joint_encoder_input_names_.begin(), joint_encoder_input_names_.end()};
}

std::unordered_set<std::string> RNNTModelNode::GetDynamicOutputs() {
  return {joint_output_names_.begin(), joint_output_names_.end()};
}

} // namespace giga_edge
