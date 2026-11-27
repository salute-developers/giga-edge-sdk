#include "giga_edge/modules/smart_home_ha/colour.h"

#include "giga_edge/modules/smart_home_ha/colour_names.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace giga_edge::ha {
namespace {
std::array<int, 3> ParseRgb(const std::string& value) {
  std::array<int, 3> rgb{};
  const char* begin = value.data();
  const char* end = begin + value.size();
  for (size_t i = 0; i < rgb.size(); ++i) {
    if (begin == end || *begin < '0' || *begin > '9') {
      throw std::runtime_error("BAD_VALUE");
    }
    const auto parsed = std::from_chars(begin, end, rgb[i]);
    if (parsed.ec != std::errc() || rgb[i] > 255) {
      throw std::runtime_error("BAD_VALUE");
    }
    begin = parsed.ptr;
    if (i + 1 < rgb.size()) {
      if (begin == end || *begin++ != ';') {
        throw std::runtime_error("BAD_VALUE");
      }
    }
  }
  if (begin != end) {
    throw std::runtime_error("BAD_VALUE");
  }
  return rgb;
}

double Linear(int channel) {
  const double value = channel / 255.0;
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

// sRGB to Oklab (https://bottosson.github.io/posts/oklab/); ColourName takes the nearest name by Euclidean distance.
std::array<double, 3> Oklab(const std::array<int, 3>& rgb) {
  const double r = Linear(rgb[0]), g = Linear(rgb[1]), b = Linear(rgb[2]);
  const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {
    0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
  };
}
} // namespace

std::string ColourName(const std::string& value) {
  static const auto colours = [] {
    std::array<std::array<double, 3>, std::size(kColourNames)> result{};
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = Oklab(kColourNames[i].rgb);
    }
    return result;
  }();
  const auto lab = Oklab(ParseRgb(value));
  const auto distance = [&](const auto& colour) {
    double sum = 0;
    for (size_t i = 0; i < lab.size(); ++i) {
      const double delta = lab[i] - colour[i];
      sum += delta * delta;
    }
    return sum;
  };
  const auto closest = std::min_element(colours.begin(), colours.end(), [&](const auto& a, const auto& b) {
    return distance(a) < distance(b);
  });
  return kColourNames[std::distance(colours.begin(), closest)].name;
}

std::string SpokenColour(const std::string& value) {
  // The RGB the model writes for these words, so a colour it set reads back as its word.
  static const std::pair<const char*, std::array<int, 3>> words[] = {
    {"белый", {255, 255, 255}},
    {"красный", {255, 0, 0}},
    {"малиновый", {220, 20, 60}},
    {"розовый", {255, 105, 180}},
    {"оранжевый", {255, 165, 0}},
    {"жёлтый", {255, 255, 0}},
    {"салатовый", {130, 255, 0}},
    {"зелёный", {0, 175, 80}},
    {"бирюзовый", {0, 206, 209}},
    {"голубой", {66, 170, 255}},
    {"синий", {0, 0, 255}},
    {"фиолетовый", {138, 43, 226}},
    {"сиреневый", {196, 131, 230}},
  };
  const auto lab = Oklab(ParseRgb(value));
  const auto distance = [&](const auto& word) {
    const auto other = Oklab(word.second);
    double sum = 0;
    for (size_t i = 0; i < lab.size(); ++i) {
      sum += (lab[i] - other[i]) * (lab[i] - other[i]);
    }
    return sum;
  };
  return std::min_element(
           std::begin(words), std::end(words), [&](const auto& a, const auto& b) {
             return distance(a) < distance(b);
           }
  )->first;
}
} // namespace giga_edge::ha
