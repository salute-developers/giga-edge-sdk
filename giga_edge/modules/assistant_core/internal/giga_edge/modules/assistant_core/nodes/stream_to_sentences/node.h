#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstddef>
#include <string>
#include <unordered_set>

namespace giga_edge {

struct StreamToSentencesParams {
  std::string terminators = ".!?…\n";
  std::size_t min_length = 12;
  std::size_t max_length = 300;
};

template<>
StreamToSentencesParams ParseConfig(const YAML::Node& config);

class StreamToSentences : public NodeBase {
public:
  StreamToSentences(const NodeBaseConfig& base_config, const StreamToSentencesParams& params);

  void ProcessOnce() override;
  void Reset() override;

  static std::unordered_set<std::string> SplitTerminators(const std::string& terminators);

private:
  size_t FirstPhraseEnd(const std::string& text) const;
  void PushPhrase(size_t length);

  REGISTER_BATCH_INPUT(
    stream_to_sentences, stream_chunk, StreamChunk, AssistantChunk, "Chunk stream with clean text deltas."
  );
  REGISTER_OUTPUT(
    stream_to_sentences, sentence, Sentence, std::string, "One speakable phrase, empty on ticks that complete none."
  );

  StreamToSentencesParams params_;
  std::unordered_set<std::string> terminators_;
  std::string pending_;
};

REGISTER_NODE(
  stream_to_sentences,
  StreamToSentences,
  StreamToSentencesParams,
  "Splits a streamed answer into whole phrases for speech synthesis."
);

} // namespace giga_edge
