#include "giga_edge/modules/tts/nodes/tts_streaming_audio_output/node.h"

#include "giga_edge/lib/environment/environment.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {

TtsStreamingAudioOutput::TtsStreamingAudioOutput(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const TtsNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {
  if (!params_.async_thread_pool.empty()) {
    if (!base_config.environments) {
      throw std::runtime_error("TtsNode: async inference requires model environments");
    }
    const auto& environments = *base_config.environments;
    const auto it = environments.find(params_.async_thread_pool);
    const auto pool = it == environments.end() ? nullptr : std::dynamic_pointer_cast<ThreadPoolEnvironment>(it->second);
    if (!pool || !pool->thread_pool) {
      throw std::runtime_error("TtsNode: no thread pool environment '" + params_.async_thread_pool + "'");
    }
    worker_pool_ = pool->thread_pool;
  }

  tts_interpreter_ = TtsBackendRegistry::CreateInterpreter(params_.backend_type);
  if (!tts_interpreter_) {
    throw std::runtime_error("Failed to create TTS interpreter");
  }

  if (!tts_interpreter_->Initialize(params_.tts_interpreter_params, data_reader, base_config.environments)) {
    throw std::runtime_error("Failed to initialize TTS interpreter");
  }
}

TtsStreamingAudioOutput::~TtsStreamingAudioOutput() {
  StopWorker();
}

void TtsStreamingAudioOutput::Reset() {
  // A reset before the first tick is the activation of the version: the previous one was removed together with its
  // phrase, as in tts_output_stub.
  close_previous_ = fresh_;
  // The phrases dropped by the reset (the started one and those waiting in the queue) each end with their own is_eos
  // in the following ticks.
  dropped_phrases_ += (state_ == State::SYNTHESIZING ? 1 : 0) + static_cast<int>(text_queue_.size());
  StopWorker();
  {
    std::lock_guard lock(chunks_mutex_);
    ready_chunks_.clear();
  }
  if (tts_interpreter_) {
    tts_interpreter_->StopSynthesis();
  }
  ResetSynthesisState();
  text_queue_ = {};
}

void TtsStreamingAudioOutput::ResetSynthesisState() {
  state_ = State::IDLE;
}

void TtsStreamingAudioOutput::ProcessOnce() {
  const auto& text_to_synthesize = GetText();
  const auto& cancel_input = GetCancel();
  const bool cancel = cancel_input.has_value() && cancel_input.value().get();
  if (const auto& voice = GetVoice()) {
    voice_ = voice->get();
  }
  if (cancel) {
    text_queue_ = {};
  }
  if (!text_to_synthesize.empty()) {
    text_queue_.push(text_to_synthesize);
  }
  fresh_ = false;
  // A tick with text has no dropped phrase: a new text is sent only after the previous one is cancelled.
  if (std::exchange(close_previous_, false) && text_to_synthesize.empty()) {
    PushAudioChunk({});
    PushIsEos(true);
    return;
  }

  if (dropped_phrases_ > 0) {
    --dropped_phrases_;
    PushAudioChunk({});
    PushIsEos(true);
    return;
  }
  if (worker_pool_) {
    ProcessOnceAsync(cancel);
    return;
  }

  if (state_ == State::SYNTHESIZING) {
    if (cancel) {
      FinishSynthesis();
      return;
    }
    StartLoop();
    return;
  }

  ApplyVoice();
  if (!text_queue_.empty()) {
    const std::string text = std::move(text_queue_.front());
    text_queue_.pop();
    try {
      tts_interpreter_->StartSynthesis(text);
    } catch (const std::exception& error) {
      std::cerr << "[tts] synthesis failed: " << error.what() << std::endl;
      tts_interpreter_->StopSynthesis();
      PushAudioChunk({});
      PushIsEos(true);
      return;
    }
    state_ = State::SYNTHESIZING;
    StartLoop();
  } else {
    PushIsEos(false);
    PushAudioChunk({});
  }
}

void TtsStreamingAudioOutput::ProcessLoopIterationImpl() {
  // A failure of the backend ends the phrase, as in async mode.
  std::optional<std::vector<float>> audio_chunk;
  try {
    if (!tts_interpreter_->IsFinished()) {
      audio_chunk = tts_interpreter_->GetAudioChunk();
    }
  } catch (const std::exception& error) {
    std::cerr << "[tts] synthesis failed: " << error.what() << std::endl;
  }
  if (!audio_chunk) {
    FinishSynthesis();
    return;
  }
  PushAudioChunk(std::move(*audio_chunk));
}

void TtsStreamingAudioOutput::ProcessLoopInterruptImpl() {
  PushIsEos(false);
  PushAudioChunk({});
}

void TtsStreamingAudioOutput::FinishSynthesis() {
  tts_interpreter_->StopSynthesis();
  ResetSynthesisState();
  PushAudioChunk({});
  PushIsEos(true);
  FinishLoop();
}

void TtsStreamingAudioOutput::ApplyVoice() {
  if (voice_ == applied_voice_) {
    return;
  }
  applied_voice_ = voice_;
  if (tts_interpreter_->SetVoice(voice_)) {
    std::cerr << "[tts] voice '" << voice_ << "'" << std::endl;
  } else {
    std::cerr << "[WARNING] tts: no voice '" << voice_ << "', the configured one" << std::endl;
  }
}

void TtsStreamingAudioOutput::ProcessOnceAsync(bool cancel) {
  if (cancel && state_ == State::SYNTHESIZING) {
    StopWorker();
    {
      std::lock_guard lock(chunks_mutex_);
      ready_chunks_.clear();
    }
    tts_interpreter_->StopSynthesis();
    ResetSynthesisState();
    PushAudioChunk({});
    PushIsEos(true);
    return;
  }
  if (state_ == State::IDLE) {
    ApplyVoice();
  }
  if (state_ == State::IDLE && !text_queue_.empty()) {
    LaunchWorker(std::move(text_queue_.front()));
    text_queue_.pop();
    state_ = State::SYNTHESIZING;
  }
  FlushChunks();
}

void TtsStreamingAudioOutput::FlushChunks() {
  // The busy flag first, then the chunks: the worker clears the flag after the last chunk.
  const bool finished = state_ == State::SYNTHESIZING && !worker_busy_;
  std::vector<std::vector<float>> chunks;
  {
    std::lock_guard lock(chunks_mutex_);
    chunks.swap(ready_chunks_);
  }
  for (auto& chunk : chunks) {
    PushAudioChunk(std::move(chunk));
  }

  if (finished) {
    StopWorker();
    ResetSynthesisState();
    PushAudioChunk({});
    PushIsEos(true);
    return;
  }
  if (chunks.empty()) {
    PushAudioChunk({});
  }
  PushIsEos(false);
}

void TtsStreamingAudioOutput::LaunchWorker(std::string text) {
  worker_cancel_ = false;
  worker_busy_ = true;
  worker_task_ = std::make_shared<Task>(
    [this, text = std::move(text)] {
      RunWorker(text);
    },
    params_.async_thread_priority
  );
  worker_pool_->Push(worker_task_);
}

void TtsStreamingAudioOutput::RunWorker(const std::string& text) {
  try {
    tts_interpreter_->StartSynthesis(text);
    while (!worker_cancel_) {
      if (tts_interpreter_->IsFinished()) {
        tts_interpreter_->StopSynthesis();
        break;
      }
      auto chunk = tts_interpreter_->GetAudioChunk();
      std::lock_guard lock(chunks_mutex_);
      ready_chunks_.push_back(std::move(chunk));
    }
  } catch (const std::exception& error) {
    std::cerr << "[tts] synthesis failed: " << error.what() << std::endl;
    tts_interpreter_->StopSynthesis();
  }
  worker_busy_ = false;
}

void TtsStreamingAudioOutput::StopWorker() {
  if (!worker_task_) {
    return;
  }
  worker_cancel_ = true;
  if (worker_pool_->Cancel(worker_task_)) {
    worker_busy_ = false;
  } else {
    worker_pool_->Wait(worker_task_);
  }
  worker_task_.reset();
}

} // namespace giga_edge
