#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>
#include "src/games/twattila/dlaa_helper/preset_writer.hpp"
using twattila::dlaa_helper::PresetWriter;
static std::promise<void> entered;
static std::promise<void> release;
static std::vector<uint32_t> saved;
static int Save(uint32_t preset) {
  if (preset == 1) {
    entered.set_value();
    release.get_future().wait();
  }
  saved.push_back(preset);
  return preset == 1 ? PresetWriter::FAILED_SAVE : PresetWriter::SAVED;
}
int main() {
  {
    PresetWriter writer;
    writer.Request(1, Save);
    if (entered.get_future().wait_for(std::chrono::seconds(3)) != std::future_status::ready) std::abort();
    writer.Request(2, Save);
    writer.Request(3, Save);
    if (writer.Result() != PresetWriter::PENDING) std::abort();
    release.set_value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (writer.Result() == PresetWriter::PENDING && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    if (writer.Result() != PresetWriter::SAVED) std::abort();
    writer.Request(4, Save);  // Destruction must drain this last selection.
  }
  if (saved != std::vector<uint32_t>{1, 3, 4}) return 1;
  std::puts("PASS: gated old save, coalesced latest selection, current result, shutdown drain; no NVIDIA profile touched");
}
