#pragma once

#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <fcntl.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace giga_edge::mcp::test_utils {

#if defined(__unix__) || defined(__APPLE__)
struct IsolatedResult {
  bool finished;
  int status;
};

inline int cleanup_fd = -1;

template<class F>
IsolatedResult RunIsolated(F scenario, std::chrono::milliseconds timeout = 2 * kWaitHangGuard) {
  int cleanup[2];
  REQUIRE(::pipe(cleanup) == 0);
  const auto child = ::fork();
  if (child == 0) {
    ::setpgid(0, 0);
    ::close(cleanup[0]);
    cleanup_fd = cleanup[1];
    int result = 0;
    try {
      scenario();
    } catch (...) {
      result = 1;
    }
    std::cout.flush();
    std::cerr.flush();
    ::_exit(result);
  }
  ::close(cleanup[1]);
  if (child < 0) {
    ::close(cleanup[0]);
    FAIL("fork failed");
  }
  ::setpgid(child, child);
  int status = 0;
  const bool finished = WaitUntil(
    [&]() {
      return ::waitpid(child, &status, WNOHANG) == child;
    },
    timeout
  );
  if (!finished) {
    ::kill(-child, SIGKILL);
    ::kill(child, SIGKILL);
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  ::fcntl(cleanup[0], F_SETFL, O_NONBLOCK);
  std::string groups;
  char buffer[128];
  ssize_t size = 0;
  while ((size = ::read(cleanup[0], buffer, sizeof(buffer))) > 0) {
    groups.append(buffer, static_cast<size_t>(size));
  }
  ::close(cleanup[0]);
  std::istringstream input(groups);
  pid_t group = 0;
  while (input >> group) {
    if (group > 0) {
      ::kill(-group, SIGKILL);
    }
  }
  return {finished, status};
}

template<class F>
void RequireIsolated(F scenario) {
  const auto result = RunIsolated(std::move(scenario));
  REQUIRE(result.finished);
  REQUIRE(WIFEXITED(result.status));
  REQUIRE(WEXITSTATUS(result.status) == 0);
}

#else

template<class F>
void RequireIsolated(F scenario) {
  scenario();
}

#endif

} // namespace giga_edge::mcp::test_utils
