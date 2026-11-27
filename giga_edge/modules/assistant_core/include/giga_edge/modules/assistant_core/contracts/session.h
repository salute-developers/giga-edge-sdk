#pragma once

#include <any>
#include <string>
#include <vector>

namespace giga_edge {

// A ready answer that does not come from the assistant (for example, from rules). The session passes it to the client
// as is: the type of commands is known to the module that produced them and to the client.
struct RulesAnswer {
  // Empty: no answer; otherwise at least one command.
  std::any commands;
  // What the answer is, for the debug log.
  std::string summary;
};

struct AssistantSessionEvent {
  enum class Kind {
    STARTED,
    REPLY,
  };
  enum class Source {
    NONE,
    RULES,
    ASSISTANT,
    FALLBACK_RULES,
    FALLBACK_TEXT,
  };
  Kind kind = Kind::STARTED;
  bool manual = false;
  std::string text;
  bool listen_again = false;
  RulesAnswer rules;
  // REPLY: who replied, and why not the assistant or why the reply is empty.
  Source source = Source::NONE;
  std::string reason;
  // REPLY: JSON session summary, if the graph collects it.
  std::string report;
  // external_data of the audio of the event's tick.
  std::any audio_metadata;
};

using AssistantSessionEvents = std::vector<AssistantSessionEvent>;

} // namespace giga_edge
