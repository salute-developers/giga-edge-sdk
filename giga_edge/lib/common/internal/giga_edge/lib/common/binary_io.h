#pragma once

#include <cstring>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {

template<class T, typename Enable = void>
struct BinaryWriter {
  static void Write(std::ostream* f, const T& value);
};

template<class T>
void WriteBinary(std::ostream* f, const T& value) {
  BinaryWriter<T>::Write(f, value);
}

template<class T>
struct BinaryWriter<T, typename std::enable_if_t<std::is_fundamental_v<T>>> {
  static void Write(std::ostream* f, const T& value) {
    f->write(reinterpret_cast<const char*>(&value), sizeof(value));
  }
};

template<class T>
struct BinaryWriter<std::vector<T>> {
  static void Write(std::ostream* f, const std::vector<T>& value) {
    WriteBinary(f, value.size());
    for (const auto& item : value) {
      WriteBinary(f, item);
    }
  }
};

template<>
struct BinaryWriter<std::string> {
  static void Write(std::ostream* f, const std::string& value) {
    WriteBinary(f, value.size());
    f->write(value.data(), static_cast<std::streamsize>(value.size()));
  }
};

template<class T>
struct BinaryWriter<std::deque<T>> {
  static void Write(std::ostream* f, const std::deque<T>& value) {
    WriteBinary(f, value.size());
    for (const auto& item : value) {
      WriteBinary(f, item);
    }
  }
};

template<class K, class V>
struct BinaryWriter<std::unordered_map<K, V>> {
  static void Write(std::ostream* f, const std::unordered_map<K, V>& value) {
    WriteBinary(f, value.size());
    for (const auto& [key, mapped] : value) {
      WriteBinary(f, key);
      WriteBinary(f, mapped);
    }
  }
};

template<class F, class S>
struct BinaryWriter<std::pair<F, S>> {
  static void Write(std::ostream* f, const std::pair<F, S>& value) {
    WriteBinary(f, value.first);
    WriteBinary(f, value.second);
  }
};

template<class T, typename Enable = void>
struct BinaryReader {
  static void Read(std::istream* f, T& value);
};

template<class T>
void ReadBinary(std::istream* f, T& value) {
  BinaryReader<T>::Read(f, value);
}

template<class T>
T ReadBinary(std::istream* f) {
  T t;
  ReadBinary(f, t);
  return t;
}

template<class T>
struct BinaryReader<T, typename std::enable_if_t<std::is_fundamental_v<T>>> {
  static void Read(std::istream* f, T& value) {
    f->read(reinterpret_cast<char*>(&value), sizeof(T));
    if (f->gcount() != sizeof(T)) {
      throw std::runtime_error("failed to read " + std::to_string(sizeof(T)) + " bytes.");
    }
  }
};

template<class T>
struct BinaryReader<std::vector<T>> {
  static void Read(std::istream* f, std::vector<T>& value) {
    typename std::vector<T>::size_type size;
    ReadBinary(f, size);
    value.resize(size);
    for (typename std::vector<T>::size_type i = 0; i < size; ++i) {
      ReadBinary(f, value[i]);
    }
  }
};

template<>
struct BinaryReader<std::string> {
  static void Read(std::istream* f, std::string& value) {
    std::string::size_type size = 0;
    ReadBinary(f, size);
    value.resize(size);
    f->read(value.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::string::size_type>(f->gcount()) != size) {
      throw std::runtime_error("failed to read " + std::to_string(size) + " bytes.");
    }
  }
};

template<class T>
struct BinaryReader<std::deque<T>> {
  static void Read(std::istream* f, std::deque<T>& value) {
    typename std::deque<T>::size_type size;
    ReadBinary(f, size);
    value.resize(size);
    for (typename std::deque<T>::size_type i = 0; i < size; ++i) {
      ReadBinary(f, value[i]);
    }
  }
};

template<class K, class V>
struct BinaryReader<std::unordered_map<K, V>> {
  static void Read(std::istream* f, std::unordered_map<K, V>& value) {
    typename std::unordered_map<K, V>::size_type size;
    ReadBinary(f, size);
    value.clear();
    for (typename std::unordered_map<K, V>::size_type i = 0; i < size; ++i) {
      K key;
      ReadBinary(f, key);
      V mapped;
      ReadBinary(f, mapped);
      value.emplace(std::move(key), std::move(mapped));
    }
  }
};

template<class F, class S>
struct BinaryReader<std::pair<F, S>> {
  static void Read(std::istream* f, std::pair<F, S>& value) {
    ReadBinary(f, value.first);
    ReadBinary(f, value.second);
  }
};

template<class T>
void ReadBinaryFile(const std::string& filepath, T& value) {
  std::ifstream f(filepath, std::ios::binary);
  ReadBinary(&f, value);
}

template<class T>
T ReadBinaryFile(const std::string& filepath) {
  T result;
  ReadBinaryFile(filepath, result);
  return result;
}

inline std::optional<std::vector<float>> BytesToFloatVector(const std::vector<char>& bytes) {
  if (bytes.size() % sizeof(float) != 0) {
    return std::nullopt;
  }
  const size_t n = bytes.size() / sizeof(float);
  std::vector<float> out(n);
  std::memcpy(out.data(), bytes.data(), bytes.size());
  return out;
}

inline std::vector<char> FloatsToBytesArray(const std::vector<float>& floats) {
  std::vector<char> out(floats.size() * sizeof(float));
  std::memcpy(out.data(), floats.data(), out.size());
  return out;
}

} // namespace giga_edge