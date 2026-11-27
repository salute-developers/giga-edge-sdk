#include "giga_edge/lib/node/loop_scheduler.h"

#include "giga_edge/lib/common/binary_io.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace giga_edge {

bool operator==(const LoopEpisode& a, const LoopEpisode& b) {
  return a.seq_id == b.seq_id && a.iters == b.iters;
}

void LoopRecorder::OnIteration(const NodeId& node, uint32_t seq_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& episodes = trace_[node];
  if (!episodes.empty() && episodes.back().seq_id == seq_id) {
    ++episodes.back().iters;
  } else {
    episodes.push_back({seq_id, 1});
  }
}

bool LoopRecorder::DecideInterrupt(const NodeId&, uint32_t, bool live_interrupt) {
  return live_interrupt;
}

const LoopTrace& LoopRecorder::Trace() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return trace_;
}

LoopPlayer::LoopPlayer(LoopTrace trace)
  : trace_(std::move(trace)) {}

void LoopPlayer::OnIteration(const NodeId& node, uint32_t seq_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = trace_.find(node);
  if (it == trace_.end() || it->second.empty()) {
    divergences_.push_back(
      "unexpected loop iteration on node '" + node + "' at seq " + std::to_string(seq_id) + " (no recorded episode)"
    );
    return;
  }

  auto& episodes = it->second;
  if (episodes.front().seq_id != seq_id) {
    divergences_.push_back(
      "loop iteration seq mismatch on node '" + node + "': recorded " + std::to_string(episodes.front().seq_id)
      + ", actual " + std::to_string(seq_id)
    );
    return;
  }

  if (--episodes.front().iters == 0) {
    episodes.pop_front();
  }
}

bool LoopPlayer::DecideInterrupt(const NodeId& node, uint32_t seq_id, bool /*live_interrupt*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = trace_.find(node);
  if (it == trace_.end() || it->second.empty()) {
    return true;
  }
  return it->second.front().seq_id != seq_id;
}

const std::vector<std::string>& LoopPlayer::Divergences() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return divergences_;
}

void BinaryWriter<LoopEpisode>::Write(std::ostream* f, const LoopEpisode& e) {
  WriteBinary(f, e.seq_id);
  WriteBinary(f, e.iters);
}

void BinaryReader<LoopEpisode>::Read(std::istream* f, LoopEpisode& e) {
  ReadBinary(f, e.seq_id);
  ReadBinary(f, e.iters);
}

} // namespace giga_edge
