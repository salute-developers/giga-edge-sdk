#pragma once

#include "data_reader.h"
#include "helpers_detail.h"

#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace giga_edge {

bool IsDirectory(const std::string& path);
std::optional<std::vector<char>> ReadFile(const std::string& file_path);
std::string ToString(IDataSpan& span); // see below for checked ptr version

namespace data_reader {

template<typename T>
std::enable_if_t<detail::is_span_ptr_v<std::remove_cv_t<std::remove_reference_t<T>>>, T&&> EnsureNotNull(T&& span_ptr) {
  if (span_ptr.get() == nullptr) {
    throw ReaderException("Unexpectedly, the pointer is null");
  }

  return std::forward<T>(span_ptr);
}

template<typename T>
std::enable_if_t<detail::is_span_ptr_v<std::remove_cv_t<std::remove_reference_t<T>>>, T&&> EnsureNotNull(
  T&& span_ptr, const std::string& path
) {
  if (span_ptr.get() == nullptr) {
    throw ReaderException("Data reader failed to read path " + path);
  }

  return std::forward<T>(span_ptr);
}

template<typename T>
std::enable_if_t<detail::is_span_ptr_v<T>, std::string> ToString(const T& span_ptr) {
  return ToString(*EnsureNotNull(span_ptr));
}

template<typename T>
detail::IfSpanPtr<T> Create(std::vector<char> data);

// External data lifetime, use carefully. IMovableDataSpan::Extract() returns a copy.
template<typename T>
detail::IfSpanPtr<T> Create(std::pair<const char*, size_t> data);

// External data lifetime, use carefully. IMovableDataSpan::Extract() returns a copy.
template<typename T>
detail::IfSpanPtr<T> Create(const char* data, size_t size) {
  return Create<T>(std::make_pair(data, size));
}

// Convenience helper for optional vector and range-pair.
template<typename T, typename U, typename E = std::enable_if_t<detail::is_data_value_v<U>>>
detail::IfSpanPtr<T> Create(std::optional<U> data) {
  if (data) {
    return Create<T>(std::move(*data));
  }

  return nullptr;
}

template<typename Span, typename Target, typename... Args>
std::enable_if_t<
  (detail::is_span_ptr_v<Span> || detail::is_span_v<Span>) && std::is_constructible_v<Target, Args&&...>,
  typename detail::Factory<Span>::Ptr>
Make(Args&&... args) {
  return detail::Factory<Span>::template Create<Target>(std::forward<Args>(args)...);
}

template<typename Span, typename Target, typename Opt, typename... Args>
std::enable_if_t<
  (detail::is_span_ptr_v<Span> || detail::is_span_v<Span>)
    && detail::is_cvref_optional_v<Opt> && std::is_constructible_v<Target, decltype(*std::declval<Opt&&>()), Args&&...>,
  typename detail::Factory<Span>::Ptr>
MakeFromOptional(Opt&& opt, Args&&... args) {
  if (opt) {
    return detail::Factory<Span>::template Create<Target>(*std::forward<Opt>(opt), std::forward<Args>(args)...);
  }

  return nullptr;
}

// Convenience helper for raw data span interface classes.
template<typename T, typename... Args>
detail::SpanPtr<T> Create(Args... args) {
  return Create<detail::SpanPtr<T>>(std::forward<Args>(args)...);
}

template<typename T>
detail::IfSpanPtr<T> ReadFileTo(const std::string& file_path) {
  return Create<T>(ReadFile(file_path));
}

} // namespace data_reader

using data_reader::Create;
using data_reader::EnsureNotNull;
using data_reader::Make;
using data_reader::MakeFromOptional;
using data_reader::ReadFileTo;
using data_reader::ToString;

// IMovableDataSpan::Extract() returns a copy.
IMovableDataSpanPtr CreateMovableDataSpan(IDataSpanPtr span);

IMovableDataSpanPtr CreateMovableTemporaryDataSpan(
  const char* data, size_t size, IDataReaderPtr::weak_type parent = IDataReaderPtr::weak_type()
);

IDataReaderPtr CreateCachingDataReader(
  IDataReaderPtr base_reader /* optional */, std::unordered_map<std::string, IDataSpanPtr> initial_cache = {}
);

IDataReaderPtr CreateAnyPathSameSpanDataReader(IDataSpanPtr span);
// ReadPersistentData returns a copy, so models that keep the span (e.g. TFLite) can be built from a temporary buffer.
IDataReaderPtr CreateAnyPathSameSpanTemporaryDataReader(const char* data, size_t size);

} // namespace giga_edge
