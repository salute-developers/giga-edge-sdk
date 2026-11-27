#include "giga_edge/lib/threadpool/threadpool.h"

#include "giga_edge/lib/common/hash_fnv1a.h"
#include "giga_edge/lib/enum/enum.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif
#ifdef _POSIX_PRIORITY_SCHEDULING
#include <sched.h>
#endif

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::ThreadPriority, "realtime", "normal");

namespace giga_edge {
namespace {

const std::size_t max_thread_name_size = 15; // documented in Linux pthread manpages

void SetCurrentThreadPriority([[maybe_unused]] ThreadPriority priority) {
#if defined(_WIN32)
  const int windows_priority = priority == ThreadPriority::REALTIME ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL;
  if (!::SetThreadPriority(::GetCurrentThread(), windows_priority)) {
    std::cerr << "[WARNING] Thread priority - " << EnumToString(priority) << ": SetThreadPriority() failed"
              << std::endl;
  }
#elif defined(__APPLE__)
  const qos_class_t qos = priority == ThreadPriority::REALTIME ? QOS_CLASS_USER_INTERACTIVE : QOS_CLASS_DEFAULT;
  if (::pthread_set_qos_class_self_np(qos, 0) != 0) {
    std::cerr << "[WARNING] Thread priority - " << EnumToString(priority) << ": pthread_set_qos_class_self_np() failed"
              << std::endl;
  }
#elif defined(_POSIX_PRIORITY_SCHEDULING)
  sched_param param{};
  int policy = SCHED_OTHER;
  if (priority == ThreadPriority::REALTIME) {
    policy = SCHED_RR;
  }

  param.sched_priority = sched_get_priority_min(policy);
  if (param.sched_priority == -1) {
    std::cerr << "[WARNING] Thread priority - " << EnumToString(priority) << ": sched_get_priority_min() failed"
              << std::endl;
    return;
  }
  int ret = sched_setscheduler(0, policy, &param);
  if (ret == -1) {
    std::cerr << "[WARNING] Thread priority - " << EnumToString(priority) << ": sched_setscheduler() failed"
              << std::endl;
  }
#else
  std::cerr << "[WARNING] Thread priorities: not implemented for unix platform without _POSIX_PRIORITY_SCHEDULING"
            << std::endl;
#endif
}

constexpr char MapNumberToSymbol(std::size_t num) {
  std::uint8_t c = num < 10 ? num + '0' : num < 36 ? num - 10 + 'a' : num < 62 ? num - 36 + 'A' : '*';
  return c;
}

void NormalizeThreadNamePrefix(std::string& name_prefix, std::string_view name_hash_source, void* addr) {
  if (!name_prefix.empty()) {
    if (name_prefix.size() >= max_thread_name_size) {
      std::cerr << "Thread name prefix '" << name_prefix << "' is too long. It will be truncated.\n";
      name_prefix.resize(14);
    }

    return;
  }

  // Make "gigaedge?????/" prefix, where ????? is a hash of the source string or threadpool address.
  name_prefix.reserve(14);
  name_prefix.assign("gigaedge"); // 8 symbols
  auto hash = name_hash_source.empty()
    ? HashValueFNV1a<std::uint32_t>(addr)
    : HashRangeFNV1a<std::uint32_t>(name_hash_source.begin(), name_hash_source.end());
  name_prefix += static_cast<char>('0' + hash % 10);
  hash /= 10;
  hash |= (hash >> 20); // fold the high bits into the consumed range, as suggested by the FNV authors
  for (unsigned i = 0; i < 4; ++i) {
    name_prefix += MapNumberToSymbol(hash & 31);
    hash >>= 5;
  }
  name_prefix += '/'; // total size is 14 symbols
}

// -1 - before, 0 - same, +1 - after.
int32_t Order(uint32_t f, uint32_t s) {
  if (f == s) {
    return 0;
  } else if ((s - f) < 0x8000'0000) {
    return -1;
  } else {
    return 1;
  }
}

} // namespace

Task::Task(
  std::function<void()> callback,
  int32_t priority,
  std::optional<uint32_t> blocking_seq_id,
  std::optional<uint32_t> blocked_seq_id
)
  : status(Status::IN_QUEUE)
  , callback(std::move(callback))
  , priority(priority)
  , blocking_seq_id(blocking_seq_id)
  , blocked_seq_id(blocked_seq_id) {}

Task::Task(std::function<void()> callback, int32_t priority)
  : status(Status::IN_QUEUE)
  , callback(std::move(callback))
  , priority(priority) {}

bool Task::Blocking(uint32_t seq_id) const {
  return blocking_seq_id.has_value() && Order(blocking_seq_id.value(), seq_id) < 0;
}

bool Task::IsBlockedBy(uint32_t seq_id) const {
  return blocked_seq_id.has_value() && Order(blocked_seq_id.value(), seq_id) >= 0;
}

ThreadPool::ThreadPool(
  int threads_count,
  bool realtime_priority,
  int realtime_threads_count,
  std::string thread_name_prefix,
  std::string_view name_hash_source
)
  : is_alive_(true)
  , threads_(threads_count)
  , blocking_tasks_(0)
  , thread_name_prefix_(std::move(thread_name_prefix)) {
  NormalizeThreadNamePrefix(thread_name_prefix_, name_hash_source, this);

  const int effective_realtime_threads_count = realtime_priority ? threads_count : realtime_threads_count;
  for (int i = 0; i < static_cast<int>(threads_.size()); ++i) {
    const ThreadPriority priority = i < effective_realtime_threads_count
      ? ThreadPriority::REALTIME
      : ThreadPriority::NORMAL;
    threads_[i] = std::thread(&ThreadPool::Run, this, priority, i);
  }
}

void ThreadPool::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    is_alive_ = false;
    for (auto& [priority, queue] : priority_queues_) {
      for (auto& task : queue) {
        if (task->status == Task::Status::IN_QUEUE) {
          task->status = Task::Status::CANCEL;
        }
      }
    }
    for (auto& task : blocked_queue_) {
      if (task->status == Task::Status::IN_QUEUE) {
        task->status = Task::Status::CANCEL;
      }
    }
  }
  cv_.notify_all();
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

ThreadPool::~ThreadPool() {
  // Qualified (non-virtual) call: the base destructor intentionally stops ThreadPool's own
  // threads; virtual dispatch is moot here (dynamic type is already ThreadPool).
  ThreadPool::Stop();
}

void ThreadPool::Push(TaskPtr task) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (barriers_.empty() || !task->IsBlockedBy(barriers_.front().seq_id)) {
    PushUnblocked(std::move(task));
  } else {
    blocked_queue_.push_back(std::move(task));
  }
}

bool ThreadPool::Cancel(const TaskPtr& task) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (task->status != Task::Status::IN_QUEUE) {
    return false;
  }
  task->status = Task::Status::CANCEL;
  cv_.notify_all();
  return true;
}

void ThreadPool::Wait(const TaskPtr& task) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [&task]() {
    return task->status == Task::Status::DONE || task->status == Task::Status::CANCEL;
  });
}

void ThreadPool::RecalcBlockingTasks() {
  assert(blocking_tasks_ == 0);
  if (barriers_.empty()) {
    return;
  }

  for (const auto& [priority, queue] : priority_queues_) {
    for (auto& task : queue) {
      assert(!task->IsBlockedBy(barriers_.front().seq_id));
      if (task->Blocking(barriers_.front().seq_id)) {
        ++blocking_tasks_;
      }
    }
  }
}

void ThreadPool::AddEventBarrier(uint32_t barrier_seq_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  barriers_.push_back({.seq_id = barrier_seq_id, .committed = false});
  if (barriers_.size() == 1) {
    RecalcBlockingTasks();
  }
}

void ThreadPool::CommitEventBarrier(uint32_t barrier_seq_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& barrier : barriers_) {
    if (barrier.seq_id == barrier_seq_id) {
      barrier.committed = true;
    }
  }
  OvercomeBarrier();
  cv_.notify_all();
}

TaskPtr ThreadPool::NextTask() const {
  for (const auto& [priority, queue] : priority_queues_) {
    for (auto& task : queue) {
      if (task->status == Task::Status::IN_QUEUE) {
        return task;
      }
    }
  }
  return nullptr;
}

void ThreadPool::OvercomeBarrier() {
  while (!priority_queues_.empty()) {
    auto& [priority, queue] = *priority_queues_.begin();

    while (!queue.empty()
           && (queue.front()->status == Task::Status::DONE || queue.front()->status == Task::Status::CANCEL)) {
      auto task_ptr = queue.front();
      if (blocking_tasks_ && !barriers_.empty() && task_ptr->Blocking(barriers_.front().seq_id)) {
        --blocking_tasks_;
      }
      queue.pop_front();
    }

    if (queue.empty()) {
      priority_queues_.erase(priority);
    } else {
      break;
    }
  }

  while (!barriers_.empty() && barriers_.front().committed && blocking_tasks_ == 0) {
    barriers_.pop_front();
    RecalcBlockingTasks();
    for (auto it = blocked_queue_.begin(); it != blocked_queue_.end();) {
      if (barriers_.empty() || !(*it)->IsBlockedBy(barriers_.front().seq_id)) {
        PushUnblocked(std::move(*it));
        it = blocked_queue_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void ThreadPool::PushUnblocked(TaskPtr task) {
  if (!barriers_.empty() && task->Blocking(barriers_.front().seq_id)) {
    ++blocking_tasks_;
  }
  priority_queues_[task->priority].push_back(std::move(task));

  cv_.notify_one();
}

void ThreadPool::Run(ThreadPriority priority, [[maybe_unused]] int id) {
  TrySetCurrentThreadName(thread_name_prefix_ + MapNumberToSymbol(id));
  SetCurrentThreadPriority(priority);

  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    cv_.wait(lock, [this]() {
      return !is_alive_ || NextTask() != nullptr;
    });
    if (!is_alive_) {
      break;
    }

    auto task = NextTask();
    task->status = Task::Status::RUNNING;
    lock.unlock();

    task->callback();

    lock.lock();
    task->status = Task::Status::DONE;
    OvercomeBarrier();
    cv_.notify_all();
  }
}

void DummyThreadPool::Push(TaskPtr task) {
  task->status = Task::Status::RUNNING;
  task->callback();
  task->status = Task::Status::DONE;
}

bool DummyThreadPool::Cancel(const TaskPtr& /* task */) {
  return false;
}

void DummyThreadPool::Wait(const TaskPtr& /* task */) {}

void DummyThreadPool::AddEventBarrier([[maybe_unused]] uint32_t last_event_id) {}

void DummyThreadPool::CommitEventBarrier([[maybe_unused]] uint32_t barrier_seq_id) {}

bool TrySetCurrentThreadName(std::string name) {
  name.resize(std::min(name.size(), max_thread_name_size));

#if defined(__APPLE__)
  return 0 == ::pthread_setname_np(name.c_str());
#elif defined(__linux__)
  return 0 == ::pthread_setname_np(::pthread_self(), name.c_str());
#else
  return false;
#endif
}

} // namespace giga_edge
