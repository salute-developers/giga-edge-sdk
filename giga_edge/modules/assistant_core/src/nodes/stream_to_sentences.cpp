#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/stream_to_sentences/node.h"
#include "giga_edge/modules/assistant_core/text.h"

namespace giga_edge {
namespace {

bool IsPhraseSpace(unsigned char byte) {
  return byte == ' ' || byte == '\n' || byte == '\t' || byte == '\r';
}

} // namespace

template<>
StreamToSentencesParams ParseConfig(const YAML::Node& config) {
  StreamToSentencesParams result;
  result.terminators = config["terminators"].as<std::string>(result.terminators);
  result.min_length = config["min_length"].as<size_t>(result.min_length);
  result.max_length = config["max_length"].as<size_t>(result.max_length);
  return result;
}

std::unordered_set<std::string> StreamToSentences::SplitTerminators(const std::string& terminators) {
  std::unordered_set<std::string> result;
  size_t i = 0;
  while (i < terminators.size()) {
    const size_t length = Utf8SequenceLength(static_cast<unsigned char>(terminators[i]));
    const size_t take = length != 0 && i + length <= terminators.size() ? length : 1;
    result.insert(terminators.substr(i, take));
    i += take;
  }
  return result;
}

StreamToSentences::StreamToSentences(const NodeBaseConfig& base_config, const StreamToSentencesParams& params)
  : NodeBase(base_config)
  , params_(params)
  , terminators_(SplitTerminators(params.terminators)) {}

void StreamToSentences::Reset() {
  pending_.clear();
}

size_t StreamToSentences::FirstPhraseEnd(const std::string& text) const {
  size_t i = 0;
  while (i < text.size()) {
    const size_t length = Utf8SequenceLength(static_cast<unsigned char>(text[i]));
    const size_t take = length != 0 && i + length <= text.size() ? length : 1;
    const size_t end = i + take;
    if (end >= params_.min_length && terminators_.count(text.substr(i, take)) > 0) {
      size_t cut = end;
      while (cut < text.size() && IsPhraseSpace(static_cast<unsigned char>(text[cut]))) {
        ++cut;
      }
      return cut;
    }
    i = end;
  }
  return 0;
}

void StreamToSentences::PushPhrase(size_t length) {
  PushSentence(pending_.substr(0, length));
  pending_.erase(0, length);
}

void StreamToSentences::ProcessOnce() {
  bool finished = false;
  for (const auto* chunk : GetStreamChunkBatch()) {
    if (chunk->meta.status == AssistantStatus::ERROR || chunk->meta.finish_reason == StreamFinishReason::CANCELLED) {
      pending_.clear();
      finished = false;
      continue;
    }
    if (const auto* delta = ChunkText(*chunk)) {
      pending_ += delta->text;
    }
    finished = finished || chunk->meta.is_final;
  }

  if (finished && !pending_.empty()) {
    PushPhrase(pending_.size());
    return;
  }

  const std::string speakable = pending_.substr(0, CompleteUtf8Prefix(pending_));
  size_t cut = FirstPhraseEnd(speakable);
  if (cut == 0 && speakable.size() >= params_.max_length) {
    cut = Utf8PrefixWithin(speakable, params_.max_length);
  }
  if (cut > 0) {
    PushPhrase(cut);
    return;
  }
  PushSentence("");
}

} // namespace giga_edge
