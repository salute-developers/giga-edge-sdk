#include "giga_edge/lib/data_reader/file_path.h"

#include "giga_edge/lib/data_reader/exceptions.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace giga_edge {

#if defined(_WIN32)

std::filesystem::path FilePath(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int length = static_cast<int>(utf8.size());
  const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), length, nullptr, 0);
  if (size <= 0) {
    throw ReaderException("Path is not valid UTF-8: " + utf8);
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, wide.data(), size);
  return std::filesystem::path(std::move(wide));
}

std::string Utf8Path(const std::filesystem::path& path) {
  const auto& wide = path.native();
  if (wide.empty()) {
    return {};
  }
  const int length = static_cast<int>(wide.size());
  const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    throw ReaderException("Path cannot be represented in UTF-8");
  }
  std::string utf8(static_cast<size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, utf8.data(), size, nullptr, nullptr);
  return utf8;
}

#else

std::filesystem::path FilePath(const std::string& utf8) {
  return std::filesystem::path(utf8);
}

std::string Utf8Path(const std::filesystem::path& path) {
  return path.native();
}

#endif

} // namespace giga_edge
