#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/threadpool/threadpool.h"
#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace giga_edge {

class TtsStreamingAudioOutput : public NodeBase {
public:
  TtsStreamingAudioOutput(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const TtsNodeParams& params
  );
  ~TtsStreamingAudioOutput() override;

  void ProcessOnce() override;
  void ProcessLoopIterationImpl() override;
  void ProcessLoopInterruptImpl() override;

  void Reset() override;

private:
  void ResetSynthesisState();
  void FinishSynthesis();
  void ApplyVoice();

  void ProcessOnceAsync(bool cancel);
  void LaunchWorker(std::string text);
  void RunWorker(const std::string& text);
  void StopWorker();
  void FlushChunks();

  REGISTER_INPUT(tts_streaming_audio_output, text, Text, std::string, "Text to synthesize.");
  REGISTER_OPTIONAL_INPUT(tts_streaming_audio_output, cancel, Cancel, bool, "Cancel synthesis.");
  REGISTER_OPTIONAL_INPUT(
    tts_streaming_audio_output,
    voice,
    Voice,
    std::string,
    "Voice of the next phrases; empty - the configured one. A started phrase keeps its voice."
  );

  REGISTER_BATCH_OUTPUT(
    tts_streaming_audio_output, audio_chunk, AudioChunk, std::vector<float>, "Synthesized audio chunks."
  );
  REGISTER_OUTPUT(tts_streaming_audio_output, is_eos, IsEos, bool, "Is eos value. True, if synthesis has ended");

private:
  TtsNodeParams params_;
  std::unique_ptr<ITtsInterpreter> tts_interpreter_;

  std::queue<std::string> text_queue_;
  std::string voice_;
  std::string applied_voice_;

  enum class State { IDLE, SYNTHESIZING } state_ = State::IDLE;
  int dropped_phrases_ = 0; // phrases dropped by a reset: each gets its own is_eos in the following ticks
  bool fresh_ = true;       // no tick yet
  // This version was activated in place of another one: the first tick without text closes the phrase the other one
  // dropped. An instance is activated only once: on a version change the manager creates it anew.
  bool close_previous_ = false;

  // Async mode: while the worker is busy it owns the interpreter, and the tick sees only the ready chunks.
  std::shared_ptr<IThreadPool> worker_pool_;
  TaskPtr worker_task_;
  std::atomic<bool> worker_busy_ = false;
  std::atomic<bool> worker_cancel_ = false;
  std::mutex chunks_mutex_;
  std::vector<std::vector<float>> ready_chunks_;
};

REGISTER_NODE(
  tts_streaming_audio_output,
  TtsStreamingAudioOutput,
  TtsNodeParams,
  true,
  "Generates streaming audio from text using a TTS engine."
);

} // namespace giga_edge
