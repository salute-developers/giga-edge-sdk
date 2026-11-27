#include "giga_edge/modules/model/backends/ncnn/tensor.h"

#include <catch.hpp>

#include <cstring>
#include <vector>

using namespace giga_edge;

TEST_CASE("NCNN tensor helpers and wrapper API", "[ncnn][tensor]") {
  SECTION("shape conversion and zero init") {
    auto one_dim = NcnnTensor::GetShapeMat({4});
    REQUIRE(NcnnTensor::GetShape(one_dim) == std::vector<int>{4});

    auto two_dim = NcnnTensor::GetShapeMat({2, 3});
    REQUIRE(NcnnTensor::GetShape(two_dim) == std::vector<int>({2, 3}));

    auto three_dim = NcnnTensor::GetShapeMat({1, 3, 4});
    REQUIRE(NcnnTensor::GetShape(three_dim) == std::vector<int>({1, 3, 4}));

    // A flat read of a multi-channel 3D tensor yields garbage between the channels.
    REQUIRE_THROWS(NcnnTensor::GetShapeMat({2, 3, 4}));

    auto zeros = NcnnTensor::Zeros({2, 3});
    auto* zeros_data = static_cast<float*>(zeros.data);
    REQUIRE(zeros_data != nullptr);
    for (int i = 0; i < 6; ++i) {
      REQUIRE(zeros_data[i] == 0.f);
    }

    REQUIRE_THROWS(NcnnTensor::GetShapeMat({1, 2, 3, 4}));
  }

  SECTION("tensor wrapper metadata and data access") {
    auto mat = NcnnTensor::Zeros({2, 3});
    auto* raw = static_cast<float*>(mat.data);
    REQUIRE(raw != nullptr);
    raw[0] = 1.5f;
    raw[5] = -2.f;

    NcnnTensor tensor(&mat, "input_a", 7);
    REQUIRE(tensor.DimsNum() == 2);
    REQUIRE(tensor.Dim(0) == 2);
    REQUIRE(tensor.Dim(1) == 3);
    REQUIRE(tensor.GetName() == "input_a");
    REQUIRE(tensor.GetIndex() == 7);
    REQUIRE(tensor.GetDataType() == Tensor::DataType::FLOAT32);
    REQUIRE(tensor.Size() == static_cast<int>(2 * 3 * sizeof(float)));

    auto* float_data = tensor.FloatData();
    REQUIRE(float_data == raw);
    REQUIRE(float_data[0] == 1.5f);
    REQUIRE(float_data[5] == -2.f);

    auto* byte_data = tensor.Data();
    REQUIRE(byte_data != nullptr);
    float first_value = 0.f;
    std::memcpy(&first_value, byte_data, sizeof(float));
    REQUIRE(first_value == 1.5f);

    REQUIRE_THROWS(tensor.IntData());
    REQUIRE_THROWS(tensor.Int64Data());

    NcnnTensor other(&mat, "input_b", 8);
    REQUIRE(tensor.IsSameType(other));
  }
}
