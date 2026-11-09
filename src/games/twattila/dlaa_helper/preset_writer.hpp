/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace twattila::dlaa_helper {

// One ordered writer, with intermediate selections coalesced. Destruction drains
// the latest request before joining; a replaced backend cannot save an old preset later.
class PresetWriter {
 public:
  static constexpr int PENDING = -1;
  static constexpr int HELD = 0;
  static constexpr int SAVED = 1;
  static constexpr int FAILED_SAVE = 2;

  void Request(uint32_t preset, int (*save)(uint32_t)) {
    const std::unique_lock lock(mutex_);
    preset_ = preset;
    ++requested_;
    result_ = PENDING;
    if (!worker_.joinable()) {
      try {
        worker_ = std::thread([this, save] {
          std::unique_lock lock(mutex_);
          for (;;) {
            changed_.wait(lock, [&] { return stopping_ || completed_ != requested_; });
            if (completed_ == requested_) return;
            const uint64_t generation = requested_;
            const uint32_t preset = preset_;
            lock.unlock();
            int result = FAILED_SAVE;
            try {
              result = save(preset);
            } catch (...) {
              // Persistence failure is reported for this selection, not a process crash.
            }
            lock.lock();
            completed_ = generation;
            if (generation == requested_) result_ = result;
          }
        });
      } catch (...) {
        completed_ = requested_;
        result_ = FAILED_SAVE;
      }
    }
    changed_.notify_all();
  }

  [[nodiscard]] int Result() const {
    const std::shared_lock lock(mutex_);
    return result_;
  }

  ~PresetWriter() {
    {
      const std::unique_lock lock(mutex_);
      stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

 private:
  mutable std::shared_mutex mutex_;
  std::condition_variable_any changed_;
  std::thread worker_;
  uint64_t requested_ = 0;
  uint64_t completed_ = 0;
  uint32_t preset_ = 0;
  int result_ = HELD;
  bool stopping_ = false;
};

}  // namespace twattila::dlaa_helper
