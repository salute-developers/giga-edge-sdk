#pragma once

#include <iostream>
#include <string_view>

namespace giga_edge {

// Writes a log line as one unformatted write: lines from parallel threads do not interleave, and formatted output
// to the shared stream would also reset its width (a data race for TSan).
inline void WriteStderrLine(std::string_view line) {
  std::cerr.write(line.data(), static_cast<std::streamsize>(line.size()));
}

} // namespace giga_edge
