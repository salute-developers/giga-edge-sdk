#include "giga_edge/modules/model/models/model_detail.h"

#include "giga_edge/lib/common/span.h"
#include "giga_edge/lib/data_reader/data_reader.h"

#include <catch.hpp>

#include <memory>
#include <utility>
#include <variant>
#include <vector>

using namespace giga_edge;
using namespace model::detail;

// These tests probe overload resolution per value category (lvalue / rvalue /
// const rvalue): every std::move (including of const variables) is the point
// of the assertion, and the functions under test inspect only, never move.
// NOLINTBEGIN(bugprone-use-after-move, performance-move-const-arg)

TEST_CASE("Generic model class, utility, IsLightweightDeferrable()", "[runtime]") {
  SECTION("empty vector - always true") {
    REQUIRE(IsLightweightDeferrable(std::vector<float>{}) == true);
    std::vector<int> data;
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const std::vector<float> const_data;
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("vector") {
    REQUIRE(IsLightweightDeferrable(std::vector<float>{1, 2, 3}) == true);
    std::vector<int> data{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(data) == false);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const std::vector<float> const_data{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == false);
  }

  SECTION("empty span - always true") {
    float f = 0;
    int i = 0;
    REQUIRE(IsLightweightDeferrable(Span<float>{&f, 0}) == true);
    Span<int> data{&i, 0};
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const Span<float> const_data;
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("zeroing special value span - always true") {
    REQUIRE(IsLightweightDeferrable(Span<float>{nullptr, 100}) == true);
    Span<int> data{nullptr, 3};
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const Span<float> const_data{nullptr, 100};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("span - always false") {
    float f = 0;
    int i = 0;
    REQUIRE(IsLightweightDeferrable(Span<float>{&f, 1}) == false);
    Span<int> data{&i, 1};
    REQUIRE(IsLightweightDeferrable(data) == false);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == false);
    const Span<float> const_data{&f, 1};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == false);
  }

  SECTION("shared_ptr<vector> - always true") {
    const std::vector<float> f{1, 2, 3};
    const std::vector<int> i{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(std::make_shared<std::vector<int>>(i)) == true);
    auto data = std::make_shared<std::vector<float>>(f);
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const auto const_data = std::make_shared<std::vector<int>>(i);
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("shared_ptr<span> - always true") {
    float f = 0;
    int i = 0;
    REQUIRE(IsLightweightDeferrable(std::make_shared<Span<float>>(&f, 1)) == true);
    auto data = std::make_shared<Span<int>>(&i, 1);
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const auto const_data = std::make_shared<Span<float>>(&f, 1);
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("variant holding empty vector - always true") {
    REQUIRE(IsLightweightDeferrable(TensorData{std::vector<float>{}}) == true);
    TensorData data = std::vector<int>{};
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const TensorData const_data = std::vector<float>{};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("variant holding vector") {
    REQUIRE(IsLightweightDeferrable(TensorData{std::vector<float>{1, 2, 3}}) == true);
    TensorData data = std::vector<int>{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(data) == false);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const TensorData const_data = std::vector<float>{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == false);
  }

  SECTION("variant holding empty span - always true") {
    const float f = 0;
    const int i = 0;
    REQUIRE(IsLightweightDeferrable(TensorData{Span<const float>{&f, 0}}) == true);
    TensorData data = Span<const int>{&i, 0};
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const TensorData const_data = Span<const float>{&f, 0};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("variant holding zeroing special value span - always true") {
    REQUIRE(IsLightweightDeferrable(TensorData{Span<const float>{nullptr, 100}}) == true);
    TensorData data = Span<const int>{nullptr, 3};
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const TensorData const_data = Span<const float>{nullptr, 100};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }

  SECTION("variant holding span - always false") {
    const float f = 0;
    const int i = 0;
    REQUIRE(IsLightweightDeferrable(TensorData{Span<const float>{&f, 1}}) == false);
    TensorData data = Span<const int>{&i, 1};
    REQUIRE(IsLightweightDeferrable(data) == false);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == false);
    const TensorData const_data = Span<const float>{&f, 1};
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == false);
  }

  SECTION("variant holding shared_ptr<vector> - always true") {
    const std::vector<float> f{1, 2, 3};
    const std::vector<int> i{1, 2, 3};
    REQUIRE(IsLightweightDeferrable(TensorData{std::make_shared<std::vector<float>>(f)}) == true);
    TensorData data = std::make_shared<std::vector<int>>(i);
    REQUIRE(IsLightweightDeferrable(data) == true);
    REQUIRE(IsLightweightDeferrable(std::move(data)) == true);
    const TensorData const_data = std::make_shared<std::vector<float>>(f);
    REQUIRE(IsLightweightDeferrable(std::move(const_data)) == true);
  }
}

TEST_CASE("Generic model class, utility, Defer()", "[runtime]") {
  SECTION("empty vector") {
    std::vector<float> data;

    auto d1 = Defer(data);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1).empty());

    auto d2 = Defer(std::move(data));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2).empty());
  }

  SECTION("vector") {
    std::vector<float> data{1, 2, 3};

    auto d1 = Defer(data);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(data));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data.empty());
  }

  SECTION("const empty span") {
    const float f = 0;
    Span<const float> span(&f, 0);
    REQUIRE(IsLightweightDeferrable(span) == true);

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<Span<const float>>(d1) == true);
    REQUIRE(std::get<Span<const float>>(d1).data() == &f);
    REQUIRE(std::get<Span<const float>>(d1).size() == 0);

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<Span<const float>>(d2) == true);
    REQUIRE(std::get<Span<const float>>(d2).data() == &f);
    REQUIRE(std::get<Span<const float>>(d2).size() == 0);
  }

  SECTION("const zeroing special value span") {
    Span<const float> span(nullptr, 100);

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<Span<const float>>(d1) == true);
    REQUIRE(std::get<Span<const float>>(d1).data() == nullptr);
    REQUIRE(std::get<Span<const float>>(d1).size() == 100);

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<Span<const float>>(d2) == true);
    REQUIRE(std::get<Span<const float>>(d2).data() == nullptr);
    REQUIRE(std::get<Span<const float>>(d2).size() == 100);
  }

  SECTION("const span") {
    std::vector<float> data{1, 2, 3};
    Span<const float> span(data.data(), data.size());

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
  }

  SECTION("span") {
    std::vector<float> data{1, 2, 3};
    Span<float> span(data.data(), data.size());

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
  }

  SECTION("shared_ptr<vector>") {
    std::vector<float> data{1, 2, 3};
    std::shared_ptr<std::vector<float>> ptr(std::shared_ptr<void>(), &data);
    REQUIRE(*ptr == std::vector<float>{1, 2, 3});
    REQUIRE(ptr.get() == &data);

    auto d1 = Defer(ptr);
    using Expected = std::shared_ptr<const std::vector<float>>;
    REQUIRE(std::holds_alternative<Expected>(d1) == true);
    REQUIRE(std::get<Expected>(d1).get() == &data);
    REQUIRE(*std::get<Expected>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
    REQUIRE(ptr != nullptr);

    auto d2 = Defer(std::move(ptr));
    REQUIRE(std::holds_alternative<Expected>(d2) == true);
    REQUIRE(std::get<Expected>(d2).get() == &data);
    REQUIRE(*std::get<Expected>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
    REQUIRE(ptr == nullptr);
  }

  SECTION("shared_ptr<const vector>") {
    std::vector<float> data{1, 2, 3};
    std::shared_ptr<const std::vector<float>> ptr(std::shared_ptr<void>(), &data);
    REQUIRE(*ptr == std::vector<float>{1, 2, 3});
    REQUIRE(ptr.get() == &data);

    auto d1 = Defer(ptr);
    using Expected = std::shared_ptr<const std::vector<float>>;
    REQUIRE(std::holds_alternative<Expected>(d1) == true);
    REQUIRE(std::get<Expected>(d1).get() == &data);
    REQUIRE(*std::get<Expected>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
    REQUIRE(ptr != nullptr);

    auto d2 = Defer(std::move(ptr));
    REQUIRE(std::holds_alternative<Expected>(d2) == true);
    REQUIRE(std::get<Expected>(d2).get() == &data);
    REQUIRE(*std::get<Expected>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
    REQUIRE(ptr == nullptr);
  }

  SECTION("variant holding vector") {
    TensorData data = std::vector<float>{1, 2, 3};

    auto d1 = Defer(data);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(std::get<std::vector<float>>(data) == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(data));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(std::get<std::vector<float>>(data).empty());
  }

  SECTION("variant holding zeroing special value const span") {
    TensorData span = Span<const float>(nullptr, 100);

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<Span<const float>>(d1) == true);
    REQUIRE(std::get<Span<const float>>(d1).data() == nullptr);
    REQUIRE(std::get<Span<const float>>(d1).size() == 100);

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<Span<const float>>(d2) == true);
    REQUIRE(std::get<Span<const float>>(d2).data() == nullptr);
    REQUIRE(std::get<Span<const float>>(d2).size() == 100);
  }

  SECTION("variant holding const span") {
    std::vector<float> data{1, 2, 3};
    TensorData span = Span<const float>(data.data(), data.size());

    auto d1 = Defer(span);
    REQUIRE(std::holds_alternative<std::vector<float>>(d1) == true);
    REQUIRE(std::get<std::vector<float>>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(span));
    REQUIRE(std::holds_alternative<std::vector<float>>(d2) == true);
    REQUIRE(std::get<std::vector<float>>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
  }

  SECTION("varian holding shared_ptr<vector>") {
    std::vector<float> data{1, 2, 3};
    TensorData ptr = std::shared_ptr<std::vector<float>>(std::shared_ptr<void>(), &data);

    auto d1 = Defer(ptr);
    using Expected = std::shared_ptr<const std::vector<float>>;
    REQUIRE(std::holds_alternative<Expected>(d1) == true);
    REQUIRE(std::get<Expected>(d1).get() == &data);
    REQUIRE(*std::get<Expected>(d1) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});

    auto d2 = Defer(std::move(ptr));
    REQUIRE(std::holds_alternative<Expected>(d2) == true);
    REQUIRE(std::get<Expected>(d2).get() == &data);
    REQUIRE(*std::get<Expected>(d2) == std::vector<float>{1, 2, 3});
    REQUIRE(data == std::vector<float>{1, 2, 3});
  }
}

// NOLINTEND(bugprone-use-after-move, performance-move-const-arg)
