#pragma once

#include "giga_edge/lib/common/binary_io.h"
#include "giga_edge/lib/node/types.h"

#include <cstdint>
#include <deque>
#include <istream>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

struct LoopEpisode {
  uint32_t seq_id;
  uint32_t iters;
};

bool operator==(const LoopEpisode& a, const LoopEpisode& b);

template<>
struct BinaryWriter<LoopEpisode> {
  static void Write(std::ostream* f, const LoopEpisode& e);
};

template<>
struct BinaryReader<LoopEpisode> {
  static void Read(std::istream* f, LoopEpisode& e);
};

using LoopTrace = std::unordered_map<NodeId, std::deque<LoopEpisode>>;

struct ILoopScheduler {
  virtual ~ILoopScheduler() = default;

  virtual void OnIteration(const NodeId& node, uint32_t seq_id) = 0;
  virtual bool DecideInterrupt(const NodeId& node, uint32_t seq_id, bool live_interrupt) = 0;
};

class LoopRecorder : public ILoopScheduler {
public:
  void OnIteration(const NodeId& node, uint32_t seq_id) override;

  bool DecideInterrupt(const NodeId& node, uint32_t seq_id, bool live_interrupt) override;

  const LoopTrace& Trace() const;

private:
  mutable std::mutex mutex_;
  LoopTrace trace_;
};

class LoopPlayer : public ILoopScheduler {
public:
  explicit LoopPlayer(LoopTrace trace);

  void OnIteration(const NodeId& node, uint32_t seq_id) override;

  bool DecideInterrupt(const NodeId& node, uint32_t seq_id, bool live_interrupt) override;

  const std::vector<std::string>& Divergences() const;

private:
  mutable std::mutex mutex_;
  LoopTrace trace_;

  std::vector<std::string> divergences_;
};

} // namespace giga_edge
