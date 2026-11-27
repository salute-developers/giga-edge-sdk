// offline_assistant_demo: a demo of an offline assistant on Home Assistant.
//
// Voice ("Giga Yozh, ..." into the microphone or from a wav), Enter or text (--text) -> recognition -> LLM with function
// calling -> the MCP Server of Home Assistant. The whole pipeline is the model graph (model/core/config.yaml and the
// parts from model/meta.json). The code only has to:
//   1. load the model with the features offline_assistant and assistant_debug_text and make sure all versions loaded;
//      set the persona of the assistant;
//   2. connect the MCP Server of Home Assistant over http (the token comes from GIGA_EDGE_HA_TOKEN);
//   3. feed the graph inputs tick by tick (50 ms) and print the request and the reply of the session;
//   4. play the reply: the graph gives its text to the part tts by itself, and the speaker of audio_io plays what
//      comes from tts_audio; a model without that part only prints the reply;
//   5. on the line /report, save the scene of the house the model sees to a file (bug_report.h).
//
// Documentation: README.md next to this file.

#include "bug_report.h"

#include "giga_edge/lib/model_layout/model_layout.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/audio_io/microphone.h"
#include "giga_edge/modules/audio_io/speaker.h"
#include "giga_edge/modules/mcp/capability.h"
#include "giga_edge/modules/mcp_http/http_connection.h"
#include "giga_edge/modules/personalization/capability.h"
#include "giga_edge/modules/personalization/environment.h"
#include "giga_edge/modules/tts/synthesized_audio.h"
#include "giga_edge/modules/web_transport_curl/curl_web_transport.h"
#include "giga_edge/nodes/registration.h"

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace giga_edge;

// The model is trained on 16 kHz mono; a 50 ms chunk is one tick of the graph.
constexpr int kSampleRateHz = 16000;
constexpr int kChunkMs = 50;
// A turn is bounded by the graph itself (the session reply_timeout is 8 s); only a hung graph takes longer.
constexpr auto kReplyTimeout = std::chrono::seconds(15);
constexpr auto kHouseTimeout = std::chrono::seconds(10);
const char* const kServerId = "ha";
constexpr std::string_view kReportCommand = "/report";

struct Args {
  bool text = false;
  std::string wav;
  std::string ha_url = "http://127.0.0.1:8123/api/mcp/assist";
  std::string model_dir = "example/offline_assistant/model";
  bool sound = true;
  std::string save_audio;
  std::string report_dir = "bug_reports";
};

Args ParseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--text") {
      args.text = true;
    } else if (arg == "--wav" && i + 1 < argc) {
      args.wav = argv[++i];
    } else if (arg == "--ha-url" && i + 1 < argc) {
      args.ha_url = argv[++i];
    } else if (arg == "--mute") {
      args.sound = false;
    } else if (arg == "--save-audio" && i + 1 < argc) {
      args.save_audio = argv[++i];
    } else if (arg == "--report-dir" && i + 1 < argc) {
      args.report_dir = argv[++i];
    } else if (arg.rfind("--", 0) == 0) {
      throw std::runtime_error(
        "usage: offline_assistant_demo [--text | --wav <file>] [--ha-url <url>] [--mute] [--save-audio <wav>] "
        "[--report-dir <dir>] [model dir]"
      );
    } else {
      args.model_dir = arg;
    }
  }
  return args;
}

// PCM16 mono 16 kHz: one chunk per tick.
std::vector<std::vector<float>> ReadWavChunks(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0) {
    throw std::runtime_error("not a wav file: " + path);
  }
  size_t pos = 12;
  bool format_checked = false;
  const char* data = nullptr;
  uint32_t data_size = 0;
  while (pos + 8 <= bytes.size()) {
    uint32_t size = 0;
    std::memcpy(&size, bytes.data() + pos + 4, 4);
    if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0) {
      uint16_t format = 0, channels = 0, bits = 0;
      uint32_t rate = 0;
      // A truncated chunk leaves zeros and fails the check.
      if (size >= 16 && pos + 8 + 16 <= bytes.size()) {
        std::memcpy(&format, bytes.data() + pos + 8, 2);
        std::memcpy(&channels, bytes.data() + pos + 10, 2);
        std::memcpy(&rate, bytes.data() + pos + 12, 4);
        std::memcpy(&bits, bytes.data() + pos + 22, 2);
      }
      if (format != 1 || channels != 1 || bits != 16 || rate != kSampleRateHz) {
        throw std::runtime_error("expected a 16 kHz mono PCM16 wav: " + path);
      }
      format_checked = true;
    } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
      data = bytes.data() + pos + 8;
      data_size = std::min<uint32_t>(size, static_cast<uint32_t>(bytes.size() - pos - 8));
      break;
    }
    pos += 8 + size + (size & 1);
  }
  if (!data) {
    throw std::runtime_error("no data in the wav file: " + path);
  }
  if (!format_checked) {
    throw std::runtime_error("no format before the data in the wav file: " + path);
  }
  const size_t samples_per_chunk = kSampleRateHz * kChunkMs / 1000;
  std::vector<std::vector<float>> chunks;
  for (size_t i = 0; i < data_size / 2; ++i) {
    if (i % samples_per_chunk == 0) {
      chunks.emplace_back();
      chunks.back().reserve(samples_per_chunk);
    }
    int16_t sample = 0;
    std::memcpy(&sample, data + i * 2, 2);
    chunks.back().push_back(sample / 32768.0f);
  }
  if (!chunks.empty()) {
    chunks.back().resize(samples_per_chunk, 0.0f);
  }
  return chunks;
}

std::string Answer(const AssistantSessionEvent& reply) {
  if (!reply.text.empty()) {
    return reply.text;
  }
  return reply.reason.empty() ? "(empty)" : "(empty: " + reply.reason + ")";
}

// Plays the synthesized replies (tts_audio) with the speaker of audio_io, opened at the sample rate of the first
// sound, and keeps them for --save-audio (PCM16 mono). The destructor waits for the playback and writes the file.
class Player {
public:
  Player(bool sound, std::string save_audio)
    : sound_(sound)
    , save_audio_(std::move(save_audio)) {}

  ~Player() {
    while (speaker_ && speaker_->IsPlaying()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (save_audio_.empty() || recorded_.empty()) {
      return;
    }
    const auto put = [](std::ofstream& out, uint32_t value, int bytes) {
      out.write(reinterpret_cast<const char*>(&value), bytes); // little endian
    };
    std::ofstream out(save_audio_, std::ios::binary);
    const auto data_size = static_cast<uint32_t>(recorded_.size() * 2);
    out.write("RIFF", 4);
    put(out, 36 + data_size, 4);
    out.write("WAVEfmt ", 8);
    put(out, 16, 4);
    put(out, 1, 2);
    put(out, 1, 2);
    put(out, sample_rate_, 4);
    put(out, sample_rate_ * 2, 4);
    put(out, 2, 2);
    put(out, 16, 2);
    out.write("data", 4);
    put(out, data_size, 4);
    for (const float sample : recorded_) {
      put(out, static_cast<uint16_t>(static_cast<int16_t>(std::clamp(sample, -1.0F, 1.0F) * 32767.0F)), 2);
    }
    if (!out) {
      std::fprintf(stderr, "offline_assistant_demo: failed to write %s\n", save_audio_.c_str());
    }
  }

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  void Play(const SynthesizedAudio& audio) {
    if (audio.samples.empty()) {
      return;
    }
    if (sound_ && !speaker_) {
#ifdef __APPLE__
      speaker_ = CreateMacosSpeaker(audio.sample_rate, 1);
#else
      speaker_ = CreateAplaySpeaker(audio.sample_rate, 1, "default");
#endif
    }
    if (speaker_) {
      speaker_->Play(audio.samples);
    }
    if (!save_audio_.empty()) {
      sample_rate_ = audio.sample_rate;
      recorded_.insert(recorded_.end(), audio.samples.begin(), audio.samples.end());
    }
  }

private:
  bool sound_;
  std::string save_audio_;
  std::shared_ptr<ISpeaker> speaker_;
  uint32_t sample_rate_ = 0;
  std::vector<float> recorded_;
};

// Exchange between the graph ticks, its outputs and the main thread: a line and a manual activation wait for their
// tick; the end of a turn (the session reply and, with synthesis, the end of its phrase) wakes up whoever is waiting.
class Turns {
public:
  struct Tick {
    std::string text;
    bool manual = false;
  };

  explicit Turns(Player* player)
    : player_(player) {}

  void Ask(std::string text) {
    std::lock_guard lock(mutex_);
    text_ = std::move(text);
    replied_ = false;
  }

  void Activate() {
    std::lock_guard lock(mutex_);
    manual_ = true;
  }

  Tick TakeTick() {
    std::lock_guard lock(mutex_);
    return {std::exchange(text_, {}), std::exchange(manual_, false)};
  }

  void OnSession(const AssistantSessionEvents& events) {
    for (const auto& event : events) {
      std::lock_guard lock(mutex_);
      if (event.kind == AssistantSessionEvent::Kind::STARTED) {
        open_ = true;
        std::printf(">> listening…\n");
      } else if (open_) {
        // A result without an open session (a reset) is not addressed to the client.
        open_ = false;
        replied_ = true;
        std::printf(">> answer: %s\n", Answer(event).c_str());
        if (player_ && !event.text.empty()) {
          speaking_ = true;
          answered_ = std::chrono::steady_clock::now();
        }
        replied_cv_.notify_all();
      }
    }
  }

  void OnAudio(const SynthesizedAudio& audio) {
    player_->Play(audio);
    std::lock_guard lock(mutex_);
    if (!speaking_) {
      return;
    }
    if (!audio.samples.empty() && answered_) {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - *answered_
      );
      std::printf(">> sound: %lld ms after the answer\n", static_cast<long long>(ms.count()));
      answered_.reset();
    }
    if (audio.is_last) {
      speaking_ = false;
      replied_cv_.notify_all();
    }
  }

  // After a clarifying question the graph opens a session by itself: the turn is over when none is open.
  bool Replied() {
    std::lock_guard lock(mutex_);
    return replied_ && !speaking_ && !open_;
  }

  bool WaitReply() {
    std::unique_lock lock(mutex_);
    return replied_cv_.wait_for(lock, kReplyTimeout, [this] {
      return replied_ && !speaking_;
    });
  }

private:
  Player* player_; // null when the model has no part tts
  std::mutex mutex_;
  std::condition_variable replied_cv_;
  std::string text_;
  bool manual_ = false;
  bool open_ = false;
  bool replied_ = true;
  bool speaking_ = false; // the graph synthesizes the reply and its phrase has not ended yet
  std::optional<std::chrono::steady_clock::time_point> answered_;
};

std::string Join(const std::vector<std::string>& names) {
  std::string result;
  for (const auto& name : names) {
    result += (result.empty() ? "" : ", ") + name;
  }
  return result;
}

// The comment of a /report line; nullopt for any other line.
std::optional<std::string> ReportComment(const std::string& line) {
  const auto end = line.find(' ');
  if (line.substr(0, end) != kReportCommand) {
    return std::nullopt;
  }
  const auto start = line.find_first_not_of(' ', end);
  return start == std::string::npos ? "" : line.substr(start);
}

int Run(const Args& args) {
  EnsureNodesRegistered();

  // The parts and their versions come from meta.json of the model. The features enable the real versions of the
  // offline_assistant parts (recognition, function calling), the text request and, if the model has the part, the
  // synthesis; without them stubs are used.
  const auto layout = ReadModelLayout(args.model_dir);
  const bool synthesis = std::any_of(layout.parts.begin(), layout.parts.end(), [](const RuntimePart& part) {
    return part.id == "tts";
  });

  // The subscription data is declared before the model: the graph is destroyed first and waits for its callbacks.
  std::optional<Player> player;
  if (synthesis) {
    player.emplace(args.sound, args.save_audio);
  }
  Turns turns(player ? &*player : nullptr);
  BugReports reports(args.report_dir);

  // The token comes from the environment, not from argv, so it is not visible in ps.
  const char* token = std::getenv("GIGA_EDGE_HA_TOKEN");
  if (!token || !*token) {
    throw std::runtime_error("no Home Assistant token: set GIGA_EDGE_HA_TOKEN (README.md, step 4)");
  }

  std::unordered_set<std::string> features = {"offline_assistant", "assistant_debug_text"};
  if (synthesis) {
    features.insert("tts");
  }
  // A male character that addresses the user formally (на «вы»): the core picks the system prompt and the voice by it.
  const auto personalization = std::make_shared<PersonalizationEnvironment>();
  EnvironmentsMap environments;
  CreatePersonalizationEnvironmentProvider(personalization)->RegisterEnvironments(environments);
  RuntimeManager manager(layout.core, layout.parts, std::move(environments), features);
  const auto persona = CreatePersonalizationCapability(manager, personalization);
  manager.RegisterCapability<IPersonalizationCapability>(persona);
  persona->SetVoice("male");
  persona->SetPronoun(PersonalizationPronoun::FORMAL);
  if (const auto state = manager.InitialUpdateState(); !state.inactive_versions.empty()) {
    std::vector<std::string> versions(state.inactive_versions.begin(), state.inactive_versions.end());
    throw std::runtime_error("model versions failed to load (see the log above): " + Join(versions));
  }
  auto runtime = manager.Model();
  reports.Watch(manager);

  // A tick closes only when all the core inputs have arrived.
  const std::unordered_set<std::string> fed = {
    "audio", "debug_text", "manual_activation", "cancel_session", "dialogue_reset"
  };
  const auto& names = runtime->InputNames();
  if (std::unordered_set<std::string>(names.begin(), names.end()) != fed) {
    throw std::runtime_error("core inputs (" + Join(names) + ") differ from what the demo feeds");
  }

  // The proxy is set with the curl environment variables (http_proxy, NO_PROXY). curl debugging stays off: it prints
  // the headers.
  auto transport = web_transport_curl::CreateCurlWebTransport(args.ha_url);
  if (!transport) {
    throw std::runtime_error("the web_transport_curl module is not built");
  }
  mcp_http::HttpConnectionOptions options;
  options.headers.emplace_back("Authorization", std::string("Bearer ") + token);
  auto house = mcp_http::CreateHttpConnection(transport, std::move(options));
  if (!house) {
    transport->Shutdown();
    throw std::runtime_error("the mcp_http module is not built");
  }

  auto capability = mcp::CreateMcpCapability(manager);
  manager.RegisterCapability<mcp::IMCPCapability>(capability);
  capability->OnConnectionAdded(kServerId, house);
  // The server is connected and has returned the tool list; if MCP has not read the house context yet, ha_catalog
  // reads the rest.
  const auto deadline = std::chrono::steady_clock::now() + kHouseTimeout;
  while (capability->GetServerIds().empty()) {
    if (std::chrono::steady_clock::now() > deadline) {
      throw std::runtime_error("Home Assistant did not answer (check the URL and the token): " + args.ha_url);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  runtime->Subscribe<std::string>({}, "user_query_text", [](const std::string& text, const std::any&) {
    if (!text.empty()) {
      std::printf(">> request: %s\n", text.c_str());
    }
  });
  runtime->Subscribe<AssistantSessionEvents>({}, "assistant_session", [&turns](const auto& events, const std::any&) {
    turns.OnSession(events);
  });
  if (synthesis) {
    runtime->Subscribe<SynthesizedAudio>({}, "tts_audio", [&turns](const SynthesizedAudio& audio, const std::any&) {
      turns.OnAudio(audio);
    });
  }

  // One tick is a Push into every core input; ticks come from a single thread (the microphone's or a dedicated one).
  int32_t chunk_id = 0;
  auto push_tick = [&](std::vector<float> audio) {
    auto tick = turns.TakeTick();
    ++chunk_id;
    runtime->Push<std::vector<float>>({}, "audio", std::move(audio), chunk_id);
    runtime->Push<std::string>({}, "debug_text", std::move(tick.text), chunk_id);
    runtime->Push<bool>({}, "manual_activation", tick.manual, chunk_id);
    runtime->Push<bool>({}, "cancel_session", false, chunk_id);
    runtime->Push<bool>({}, "dialogue_reset", false, chunk_id);
  };

  std::atomic<bool> stop{false};
  std::thread ticks;
  std::shared_ptr<IMicrophone> mic;
  if (!args.wav.empty()) {
    // The recording at microphone pace, then silence until the session reply arrives.
    auto chunks = ReadWavChunks(args.wav);
    const std::vector<float> silence(kSampleRateHz * kChunkMs / 1000, 0.0f);
    turns.Ask({});
    auto next = std::chrono::steady_clock::now();
    const auto deadline = next + std::chrono::milliseconds(kChunkMs) * chunks.size() + kReplyTimeout;
    for (size_t i = 0; std::chrono::steady_clock::now() < deadline; ++i) {
      if (i >= chunks.size() && turns.Replied()) {
        break;
      }
      push_tick(i < chunks.size() ? chunks[i] : silence);
      next += std::chrono::milliseconds(kChunkMs);
      std::this_thread::sleep_until(next);
    }
    const bool replied = turns.Replied();
    if (!replied) {
      std::fprintf(stderr, "offline_assistant_demo: no answer to %s\n", args.wav.c_str());
    }
    house->close();
    capability->OnConnectionRemoved(kServerId);
    transport->Shutdown();
    return replied ? 0 : 1;
  }
  if (args.text) {
    ticks = std::thread([&] {
      const std::vector<float> silence(kSampleRateHz * kChunkMs / 1000, 0.0f);
      auto next = std::chrono::steady_clock::now();
      while (!stop.load()) {
        push_tick(silence);
        next += std::chrono::milliseconds(kChunkMs);
        std::this_thread::sleep_until(next);
      }
    });
  } else {
    auto on_chunk = [&](std::vector<float> samples, int32_t) {
      push_tick(std::move(samples));
    };
#ifdef __APPLE__
    mic = CreateMacosMicrophone(kSampleRateHz, kChunkMs, on_chunk);
#else
    mic = CreateArecordMicrophone(kSampleRateHz, kChunkMs, "default", on_chunk);
#endif
  }

  std::printf(
    "offline_assistant_demo: model loaded, Home Assistant connected. %s (Ctrl-D to exit)\n",
    args.text ? "Type a request" : "Say «Гига Ёж» and a command (or press Enter and speak)"
  );
  bool hung = false;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (const auto comment = ReportComment(line)) {
      try {
        std::printf(">> report: %s\n", reports.Write(*comment).c_str());
      } catch (const std::exception& e) {
        std::printf(">> report failed: %s\n", e.what());
      }
      continue;
    }
    if (!args.text) {
      turns.Activate();
      continue;
    }
    if (line.find_first_not_of(" \t\r") == std::string::npos) {
      continue;
    }
    turns.Ask(line);
    if (!turns.WaitReply()) {
      std::fprintf(
        stderr, "offline_assistant_demo: no answer in %lld s\n", static_cast<long long>(kReplyTimeout.count())
      );
      hung = true;
    }
  }

  stop.store(true);
  if (ticks.joinable()) {
    ticks.join();
  }
  if (mic) {
    mic->Stop();
  }
  house->close();
  capability->OnConnectionRemoved(kServerId);
  transport->Shutdown();
  return hung ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  try {
    return Run(ParseArgs(argc, argv));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "offline_assistant_demo: %s\n", e.what());
    return 1;
  }
}
