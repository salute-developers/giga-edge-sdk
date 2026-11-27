#pragma once

namespace {

// The visitor helper keeps its canonical lowercase spelling from the std::visit idiom.
template<class... Ts>
// NOLINTNEXTLINE(readability-identifier-naming)
struct overloaded : Ts... {
  using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace