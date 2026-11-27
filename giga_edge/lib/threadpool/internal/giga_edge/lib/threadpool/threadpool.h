#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace giga_edge {

struct Task {
  enum class Status {
    IN_QUEUE = 0,
    RUNNING = 1,
    DONE = 2,
    CANCEL = 3,
  };
  std::atomic<Status> status;

  std::function<void()> callback;
  int32_t priority;
  std::optional<uint32_t> blocking_seq_id; // if blocking_seq_id in queue, barrier after blocking_seq_id will hold
  std::optional<uint32_t> blocked_seq_id;  // if blocked_seq_id < first barrier, will not run task

  explicit Task(
    std::function<void()> callback,
    int32_t priority,
    std::optional<uint32_t> blocking_seq_id,
    std::optional<uint32_t> blocked_seq_id
  );

  explicit Task(std::function<void()> callback, int32_t priority);

  bool Blocking(uint32_t blocking_seq_id) const;
  bool IsBlockedBy(uint32_t blocking_seq_id) const;
};

enum class ThreadPriority { REALTIME, NORMAL };

using TaskId = std::string;
using TaskPtr = std::shared_ptr<Task>;

class IThreadPool {
public:
  virtual void Push(TaskPtr task) = 0;
  // Returns whether cancelation is successful - cannot cancel a task that is already running or finished.
  virtual bool Cancel(const TaskPtr& task) = 0;
  virtual void Wait(const TaskPtr& task) = 0;

  // First all tasks with seq_id < barrier_seq_id should be processed.
  virtual void AddEventBarrier(uint32_t barrier_seq_id) = 0;

  // Commits the barrier added with this seq_id. Several barriers may be pending at once: they are passed in the
  // order they were added, each one only after it is committed.
  virtual void CommitEventBarrier(uint32_t barrier_seq_id) = 0;

  virtual void Stop() = 0;

  virtual ~IThreadPool() = default;
};

class ThreadPool : public IThreadPool {
public:
  // The length of full thread name must not exceed 15 chars.
  // If no prefix provided then use name_hash_source (if not empty) to generate name prefix.
  explicit ThreadPool(
    int threads_count,
    bool realtime = false,
    int realtime_threads_count = 0,
    std::string thread_name_prefix = "",
    std::string_view name_hash_source = {}
  );

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() override;

  void Push(TaskPtr task) override;
  bool Cancel(const TaskPtr& task) override;
  void Wait(const TaskPtr& task) override;

  void AddEventBarrier(uint32_t barries_seq_id) override;

  void CommitEventBarrier(uint32_t barrier_seq_id) override;

  void Stop() override;

private:
  void Run(ThreadPriority priority, int id);

  void PushUnblocked(TaskPtr task);
  TaskPtr NextTask() const;
  void OvercomeBarrier();
  void RecalcBlockingTasks();

  struct Barrier {
    uint32_t seq_id;
    bool committed;
  };

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool is_alive_;
  std::vector<std::thread> threads_;

  // Map to handle priority (higher priority -> first), deque to handle FIFO order.
  std::map<int32_t, std::deque<TaskPtr>, std::greater<int32_t>> priority_queues_;

  int32_t blocking_tasks_;
  std::deque<TaskPtr> blocked_queue_;
  std::deque<Barrier> barriers_;

  std::string thread_name_prefix_;
};

class DummyThreadPool : public IThreadPool {
public:
  void Push(TaskPtr task) override;
  bool Cancel(const TaskPtr& task) override;
  void Wait(const TaskPtr& task) override;

  void AddEventBarrier(uint32_t barrier_seq_id) override;
  void CommitEventBarrier(uint32_t barrier_seq_id) override;

  void Stop() override {}
};

bool TrySetCurrentThreadName(std::string name);

} // namespace giga_edge
