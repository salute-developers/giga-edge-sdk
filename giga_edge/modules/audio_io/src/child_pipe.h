#pragma once

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace giga_edge {

// A pipe to a child program, like popen/pclose but without a shell: the arguments go to exec as is, so a value
// from the config (a device name) is not parsed as a command.
struct ChildPipe {
  FILE* stream = nullptr;
  pid_t pid = -1;
};

inline void WaitChild(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}

// read_output: read the program's stdout, otherwise write to its stdin. On error stream is null and errno is set.
inline ChildPipe OpenChildPipe(const std::vector<std::string>& args, bool read_output) {
  int fds[2];
  // O_CLOEXEC: other child processes do not inherit the pipe ends, otherwise EOF would never arrive.
  if (pipe2(fds, O_CLOEXEC) != 0) {
    return {};
  }
  const int parent_end = read_output ? fds[0] : fds[1];
  int child_end = read_output ? fds[1] : fds[0];
  const int child_fd = read_output ? STDOUT_FILENO : STDIN_FILENO;
  // The process has stdin or stdout closed, and the pipe end took its number: dup2 onto itself would not clear
  // O_CLOEXEC and exec would close it, so move it to another number, as popen in glibc does.
  if (child_end == child_fd) {
    const int moved = fcntl(child_end, F_DUPFD_CLOEXEC, 0);
    const int error = errno;
    close(child_end);
    if (moved < 0) {
      close(parent_end);
      errno = error;
      return {};
    }
    child_end = moved;
  }

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid = -1;
  // The program starts with no signals blocked instead of the mask of the calling thread (the speaker thread blocks
  // SIGPIPE for itself).
  sigset_t no_signals;
  sigemptyset(&no_signals);
  posix_spawnattr_t attr;
  int error = posix_spawnattr_init(&attr);
  if (error == 0) {
    posix_spawn_file_actions_t actions;
    error = posix_spawn_file_actions_init(&actions);
    if (error == 0) {
      error = posix_spawnattr_setsigmask(&attr, &no_signals);
      if (error == 0) {
        error = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
      }
      if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&actions, child_end, child_fd);
      }
      if (error == 0) {
        error = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);
      }
      posix_spawn_file_actions_destroy(&actions);
    }
    posix_spawnattr_destroy(&attr);
  }
  close(child_end);
  if (error != 0) {
    close(parent_end);
    errno = error;
    return {};
  }

  FILE* stream = fdopen(parent_end, read_output ? "r" : "w");
  if (stream == nullptr) {
    error = errno;
    // The program sees the closed pipe and exits.
    close(parent_end);
    WaitChild(pid);
    errno = error;
    return {};
  }
  return {stream, pid};
}

// Closes the pipe and waits for the program, like pclose.
inline void CloseChildPipe(ChildPipe& child) {
  if (child.stream == nullptr) {
    return;
  }
  std::fclose(child.stream);
  WaitChild(child.pid);
  child = {};
}

} // namespace giga_edge
