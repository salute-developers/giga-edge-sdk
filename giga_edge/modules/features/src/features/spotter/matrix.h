#pragma once

#include "giga_edge/lib/common/os.h"

#include <cstddef>
#include <vector>

#ifdef OS_APPLE
#import <Accelerate/Accelerate.h>
#else
#include <Eigen/Core>
#endif

namespace giga_edge {

class Matrix {
public:
#ifdef OS_APPLE
  Matrix Transpose() const {
    Matrix matr(cols_, rows_);
    vDSP_mtrans(
      data_.data(),
      1, // input stride
      matr.data_.data(),
      1, // output stride
      vDSP_Length(cols_),
      vDSP_Length(rows_)
    );
    return matr;
  }

  std::vector<float> RowMean() const {
    std::vector<float> result;
    for (int r = 0; r < rows_; r += 1) {
      float value;
      vDSP_meanv(
        data_.data() + r * cols_,
        1, // input stride
        &value,
        vDSP_Length(cols_)
      );
      result.push_back(value);
    }
    return result;
  }

  size_t Rows() const {
    return rows_;
  }

  size_t Cols() const {
    return cols_;
  }

  std::vector<float> Flatten() const {
    return data_;
  }

  float operator()(size_t row, size_t col) const {
    return data_[row * cols_ + col];
  }

  float& operator()(size_t row, size_t col) {
    return data_[row * cols_ + col];
  }

  Matrix operator*(const Matrix& another) {
    Matrix matr(rows_, another.cols_);
    vDSP_mmul(
      data_.data(),
      1,
      another.data_.data(),
      1,
      matr.data_.data(),
      1,
      vDSP_Length(rows_),
      vDSP_Length(another.cols_),
      vDSP_Length(cols_)
    );
    return matr;
  }

  Matrix(size_t rows, size_t cols)
    : rows_(rows)
    , cols_(cols)
    , data_(rows * cols, 0) {}
#else
  Matrix(size_t rows, size_t cols)
    : impl_(rows, cols) {}

  Matrix Transpose() const {
    return Matrix(impl_.transpose().eval());
  }

  std::vector<float> RowMean() const {
    auto matr = impl_.rowwise().mean().eval();
    std::vector<float> result(matr.rows() * matr.cols());
    Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> >(
      result.data(), matr.rows(), matr.cols()
    ) = matr;

    return result;
  }

  size_t Rows() const {
    return impl_.rows();
  }

  size_t Cols() const {
    return impl_.cols();
  }

  std::vector<float> Flatten() const {
    std::vector<float> features(impl_.rows() * impl_.cols());
    Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> >(
      features.data(), impl_.rows(), impl_.cols()
    ) = impl_;
    return features;
  }

  float operator()(size_t row, size_t col) const {
    return impl_(row, col);
  }

  float& operator()(size_t row, size_t col) {
    return impl_(row, col);
  }

  Matrix operator*(const Matrix& another) {
    return Matrix(impl_ * another.impl_);
  }
#endif

private:
#ifdef OS_APPLE
  size_t rows_;
  size_t cols_;
  std::vector<float> data_;
#else
  explicit Matrix(Eigen::MatrixXf&& matr)
    : impl_(std::move(matr)) {}

  Eigen::MatrixXf impl_;
#endif
};
} // namespace giga_edge
