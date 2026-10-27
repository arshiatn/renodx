/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cmath>
#include <utility>

// Matrix helpers shared by the DLSS addons (frame generation's camera, ray tracing's cameras).
namespace talesofarise::frame_generation {

// A 4 x 4 matrix's inverse (row-major): Gauss-Jordan with partial pivoting, in doubles.
inline std::array<float, 16> Inverse(const std::array<float, 16>& matrix) {
  std::array<std::array<double, 8>, 4> rows = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) rows[row][column] = matrix[(row * 4) + column];
    rows[row][4 + row] = 1.;
  }
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
    }
    std::swap(rows[pivot], rows[column]);
    const double scale = (rows[column][column] != 0. ? 1. / rows[column][column] : 0.);
    for (double& value : rows[column]) value *= scale;
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = rows[row][column];
      for (int index = 0; index < 8; ++index) rows[row][index] -= factor * rows[column][index];
    }
  }
  std::array<float, 16> result = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) result[(row * 4) + column] = static_cast<float>(rows[row][4 + column]);
  }
  return result;
}

}  // namespace talesofarise::frame_generation
