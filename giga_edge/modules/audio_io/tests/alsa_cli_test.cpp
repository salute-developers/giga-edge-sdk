#include "giga_edge/modules/audio_io/microphone.h"
#include "giga_edge/modules/audio_io/speaker.h"

#include <catch.hpp>

#if defined(__linux__) && !defined(__ANDROID__)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

namespace fs = std::filesystem;

// Fake arecord and aplay first in PATH: they write their arguments to a file, arecord gives 100 ms of 16 kHz silence.
class FakeAlsaTools {
public:
  FakeAlsaTools() {
    std::string dir = (fs::temp_directory_path() / "giga_edge_audio_io_XXXXXX").string();
    REQUIRE(::mkdtemp(dir.data()) != nullptr);
    dir_ = dir;
    WriteScript("arecord", "printf '%s\\n' \"$@\" > " + Path("args") + "\nhead -c 3200 /dev/zero\n");
    WriteScript("aplay", "printf '%s\\n' \"$@\" > " + Path("args") + "\ncat > " + Path("played") + "\n");
    const char* path = std::getenv("PATH");
    old_path_ = path != nullptr ? path : "";
    ::setenv("PATH", (dir_.string() + ":" + old_path_).c_str(), 1);
  }

  ~FakeAlsaTools() {
    ::setenv("PATH", old_path_.c_str(), 1);
    std::error_code error;
    fs::remove_all(dir_, error);
  }

  std::string Args() const {
    std::ifstream in(dir_ / "args");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

  uintmax_t PlayedBytes() const {
    return fs::file_size(dir_ / "played");
  }

  // aplay that exits on its first start without reading anything: the write of the speaker gets EPIPE. The next
  // starts play.
  void AplayExitsOnFirstStart() const {
    WriteScript(
      "aplay",
      "echo >> " + Path("starts") + "\n[ -e " + Path("exited") + " ] || { touch " + Path("exited")
        + "; exit 0; }\ncat > " + Path("played") + "\n"
    );
  }

  // aplay that writes the mask of its blocked signals (SigBlk of /proc, hex) and plays.
  void AplayReportsBlockedSignals() const {
    WriteScript("aplay", "grep '^SigBlk:' /proc/$$/status > " + Path("blocked") + "\ncat > " + Path("played") + "\n");
  }

  uint64_t AplayBlockedSignals() const {
    std::ifstream in(dir_ / "blocked");
    std::string key;
    std::string mask;
    in >> key >> mask;
    return std::stoull(mask, nullptr, 16);
  }

  size_t AplayStarts() const {
    std::ifstream in(dir_ / "starts");
    return static_cast<size_t>(std::count(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), '\n'));
  }

  // A shell would run touch from any of the values and create this file.
  std::vector<std::string> InjectedValues() const {
    const std::string touch = "touch " + Marker().string();
    return {
      "default & " + touch + " #",
      "default; " + touch + " #",
      "$(" + touch + ")",
      "`" + touch + "`",
      "'default' \"hw:0\" & " + touch + " #",
      "default\n" + touch + " #",
    };
  }

  fs::path Marker() const {
    return dir_ / "injected";
  }

private:
  std::string Path(const std::string& name) const {
    return "'" + (dir_ / name).string() + "'";
  }

  void WriteScript(const std::string& name, const std::string& body) const {
    {
      std::ofstream out(dir_ / name);
      out << "#!/bin/sh\n" << body;
    }
    fs::permissions(dir_ / name, fs::perms::owner_all);
  }

  fs::path dir_;
  std::string old_path_;
};

// A process with stdin/stdout closed, as if started with <&- >&-: the pipe to the program gets their numbers.
// Catch output and the checks come after they are restored.
class ClosedStdio {
public:
  explicit ClosedStdio(std::vector<int> fds)
    : fds_(std::move(fds)) {
    std::fflush(stdout);
    for (const int fd : fds_) {
      saved_.push_back(::dup(fd));
    }
    for (const int fd : fds_) {
      ::close(fd);
    }
  }

  ~ClosedStdio() {
    for (size_t i = 0; i < fds_.size(); ++i) {
      ::dup2(saved_[i], fds_[i]);
      ::close(saved_[i]);
    }
  }

  ClosedStdio(const ClosedStdio&) = delete;
  ClosedStdio& operator=(const ClosedStdio&) = delete;

private:
  std::vector<int> fds_;
  std::vector<int> saved_;
};

// One argument per line, as the fake tool wrote them.
std::string ExpectedArgs(const std::string& device, int channels, int sample_rate_hz) {
  return "-q\n-D\n" + device + "\n-t\nraw\n-f\nS16_LE\n-c\n" + std::to_string(channels) + "\n-r\n"
    + std::to_string(sample_rate_hz) + "\n";
}

} // namespace

TEST_CASE("arecord gets the capture device as one argument, without a shell", "[audio_io][arecord]") {
  FakeAlsaTools tools;
  for (const auto& device : tools.InjectedValues()) {
    INFO("capture device: " << device);
    fs::remove(tools.Marker());
    // The callbacks come from the reader thread: only counters there, the checks are here.
    std::atomic<int> chunks{0};
    std::atomic<int> silent_chunks{0};
    std::promise<void> finished;
    auto microphone = CreateArecordMicrophone(
      16000,
      50,
      device,
      [&chunks, &silent_chunks](std::vector<float> samples, int32_t /*chunk_id*/) {
        ++chunks;
        silent_chunks += samples == std::vector<float>(800, 0.0f) ? 1 : 0;
      },
      [&finished]() {
        finished.set_value();
      }
    );
    REQUIRE(finished.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    microphone.reset();

    CHECK(chunks.load() == 2);
    CHECK(silent_chunks.load() == 2);
    CHECK(tools.Args() == ExpectedArgs(device, 1, 16000));
    CHECK_FALSE(fs::exists(tools.Marker()));
  }
}

TEST_CASE("aplay gets the playback device as one argument, without a shell", "[audio_io][aplay]") {
  FakeAlsaTools tools;
  for (const auto& device : tools.InjectedValues()) {
    INFO("playback device: " << device);
    fs::remove(tools.Marker());
    auto speaker = CreateAplaySpeaker(22050, 1, device);
    speaker->Play(std::vector<float>(1600, 0.5f));
    // The destructor plays the queue out, closes the pipe and waits for aplay.
    speaker.reset();

    CHECK(tools.PlayedBytes() == 1600 * sizeof(int16_t));
    CHECK(tools.Args() == ExpectedArgs(device, 1, 22050));
    CHECK_FALSE(fs::exists(tools.Marker()));
  }
}

TEST_CASE("aplay that has exited fails the write and the next chunk starts a new aplay", "[audio_io][aplay]") {
  FakeAlsaTools tools;
  tools.AplayExitsOnFirstStart();
  auto speaker = CreateAplaySpeaker(22050, 1, "default");
  // More than a pipe holds: the write to the first aplay cannot complete. A SIGPIPE would end the whole process here.
  speaker->Play(std::vector<float>(100000, 0.5f));
  speaker->Play(std::vector<float>(1600, 0.5f));
  speaker.reset();

  CHECK(tools.AplayStarts() == 2);
  CHECK(tools.PlayedBytes() == 1600 * sizeof(int16_t));
}

TEST_CASE("aplay starts without the SIGPIPE that the speaker thread blocks", "[audio_io][aplay]") {
  FakeAlsaTools tools;
  tools.AplayReportsBlockedSignals();
  auto speaker = CreateAplaySpeaker(22050, 1, "default");
  speaker->Play(std::vector<float>(1600, 0.5f));
  speaker.reset();

  CHECK(tools.PlayedBytes() == 1600 * sizeof(int16_t));
  CHECK((tools.AplayBlockedSignals() & (uint64_t{1} << (SIGPIPE - 1))) == 0);
}

TEST_CASE("arecord records when the process has stdin and stdout closed", "[audio_io][arecord]") {
  FakeAlsaTools tools;
  std::atomic<int> chunks{0};
  std::promise<void> finished;
  auto ended = finished.get_future();
  bool ended_in_time = false;
  {
    const ClosedStdio closed({STDIN_FILENO, STDOUT_FILENO});
    auto microphone = CreateArecordMicrophone(
      16000,
      50,
      "default",
      [&chunks](std::vector<float> /*samples*/, int32_t /*chunk_id*/) {
        ++chunks;
      },
      [&finished]() {
        finished.set_value();
      }
    );
    ended_in_time = ended.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    microphone.reset();
  }
  REQUIRE(ended_in_time);
  CHECK(chunks.load() == 2);
}

TEST_CASE("aplay plays when the process has stdin closed", "[audio_io][aplay]") {
  FakeAlsaTools tools;
  {
    const ClosedStdio closed({STDIN_FILENO});
    auto speaker = CreateAplaySpeaker(22050, 1, "default");
    speaker->Play(std::vector<float>(1600, 0.5f));
    speaker.reset();
  }
  CHECK(tools.PlayedBytes() == 1600 * sizeof(int16_t));
}

TEST_CASE("arecord that cannot start is an error at creation", "[audio_io][arecord]") {
  const char* path = std::getenv("PATH");
  const std::string old_path = path != nullptr ? path : "";
  ::setenv("PATH", "/nonexistent", 1);
  CHECK_THROWS_WITH(
    CreateArecordMicrophone(16000, 50, "default", [](std::vector<float>, int32_t) {}),
    Catch::Contains("Failed to start arecord for capture device 'default'")
  );
  ::setenv("PATH", old_path.c_str(), 1);
}

#endif
