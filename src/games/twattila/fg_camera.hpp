/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "../talesofarise_dlss/frame_generation.hpp"

// Frame generation's camera from a 64-bit Total War camera buffer as written (Shogun 2 CE's camera_VS_PS, Warhammer
// III's camera: position c0, view c2, projection c6, inv_view c18, near / far c34; HLSL column-major registers, register
// c + j holding column j of a row-vector matrix), and the motion to the last frame's (its view-projection kept).
namespace twattila::fg_camera {

class Tracker {
 public:
  // This frame's camera (the projection without its jitter).
  talesofarise::frame_generation::Camera Next(const float* c) {
    const auto matrix = [c](size_t first) {
      std::array<float, 16> result;
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) result[row * 4 + column] = c[(first + column) * 4 + row];
      }
      return result;
    };
    auto projection = matrix(6);
    projection[8] = 0.f;
    projection[9] = 0.f;
    const auto view = matrix(2);
    const auto inverse_view = matrix(18);
    std::array<float, 16> view_projection = {};
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        for (size_t k = 0; k < 4; ++k) view_projection[row * 4 + column] += view[row * 4 + k] * projection[k * 4 + column];
      }
    }
    const float near_plane = c[34 * 4];
    talesofarise::frame_generation::Camera camera = {
        .view_to_clip = projection,
        .clip_to_previous_clip = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f},
        .position = {c[0], c[1], c[2]},
        .right = {inverse_view[0], inverse_view[1], inverse_view[2]},
        .up = {inverse_view[4], inverse_view[5], inverse_view[6]},
        .forward = {inverse_view[8], inverse_view[9], inverse_view[10]},
        .near_plane = near_plane,
        .far_plane = c[34 * 4 + 1],
        // Reversed when the near plane's depth is 1 (z / w there: [2][2] + [3][2] / near).
        .depth_inverted = projection[10] + projection[14] / near_plane > 0.5f,
    };
    if (previous_view_projection) {
      // Row vectors: clip x inverse(this frame's) x last frame's.
      const auto inverse = talesofarise::frame_generation::Inverse(view_projection);
      auto& to_previous = camera.clip_to_previous_clip;
      to_previous = {};
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          for (size_t k = 0; k < 4; ++k) to_previous[row * 4 + column] += inverse[row * 4 + k] * (*previous_view_projection)[k * 4 + column];
        }
      }
    }
    previous_view_projection = view_projection;
    return camera;
  }

 private:
  std::optional<std::array<float, 16>> previous_view_projection;
};

}  // namespace twattila::fg_camera
