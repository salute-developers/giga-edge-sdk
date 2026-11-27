#pragma once

#include <string>

namespace giga_edge {

struct PreSynthesizedTtsInterpreterParams {
  std::string metadata_path;
  std::string audio_files_dir;
};

} // namespace giga_edge
