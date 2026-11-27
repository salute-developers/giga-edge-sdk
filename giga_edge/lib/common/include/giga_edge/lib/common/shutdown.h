#pragma once

namespace giga_edge {

// Shutdown drops unfinished work; a graceful finish is the owner's job beforehand.
// Called from the owner's thread at any moment: implementations are thread-safe, idempotent
// and bounded in time.
class IShutdown {
public:
  virtual ~IShutdown() = default;

  virtual void Shutdown() noexcept = 0;
};

} // namespace giga_edge
