/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Live access to Unreal's r.ScreenPercentage (UE 4.26, monolithic exe). Its static
// TAutoConsoleVariable<float> in the exe's data is {vtable, Target, Ref}: Target is the heap
// FConsoleVariable<float> {vtable, FString Help, ...}, Ref its {game-thread, render-thread} value.
// Only the exe image is scanned, never the heap:
//  1. statics whose Target's help text starts like r.ScreenPercentage's;
//  2. otherwise the static constructed by the code that loads L"r.ScreenPercentage".
// Small steps on the present thread; other memory is read only through ReadProcessMemory.
namespace expeditionsrome::engine_scale {

class Finder {
 public:
  enum class Phase : uint8_t { IDLE,
                               STATICS,
                               HELP,
                               NAME,
                               CODE,
                               FOUND,
                               FAILED };

  Phase phase() const { return phase_; }
  bool Searching() const { return phase_ >= Phase::STATICS && phase_ <= Phase::CODE; }
  const std::string& status() const { return status_; }
  // What was found where, or what was seen when nothing was; for the log.
  const std::string& details() const { return details_; }

  void Start() {
    *this = {};
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    image_ = {.begin = base, .end = base + nt->OptionalHeader.SizeOfImage};
    const auto& exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.VirtualAddress != 0) {
      functions_ = {.begin = base + exceptions.VirtualAddress, .end = base + exceptions.VirtualAddress + exceptions.Size};
    }
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
      const Range range = {
          .begin = base + section->VirtualAddress,
          .end = base + section->VirtualAddress + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData),
      };
      if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
        code_.push_back(range);
      } else if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
        writable_.push_back(range);
      } else if ((section->Characteristics & IMAGE_SCN_MEM_READ) != 0) {
        readonly_.push_back(range);
      }
    }
    if (readonly_.empty() || writable_.empty() || code_.empty()) {
      Fail("the game's exe sections are not readable");
      return;
    }
    Begin(Phase::STATICS, writable_);
  }

  // Works in small units for about `budget` (at least one unit); true while searching.
  bool Step(std::chrono::microseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (Searching()) {
      switch (phase_) {
        case Phase::STATICS: StepStatics(); break;
        case Phase::HELP:    StepHelp(); break;
        case Phase::NAME:    StepName(); break;
        default:             StepCode(); break;
      }
      if (Searching() && std::chrono::steady_clock::now() >= deadline) {
        constexpr std::array<const char*, 4> WHAT = {"console variables", "help texts", "name", "code"};
        const uint64_t done = (phase_ == Phase::HELP ? checked_ : scanned_);
        const uint64_t total = (phase_ == Phase::HELP ? statics_.size() : total_);
        status_ = std::format("Searching the game's exe: {} {}%", WHAT[static_cast<size_t>(phase_) - 1],
                              total == 0 ? 0 : done * 100 / total);
        return true;
      }
    }
    return false;
  }

  std::optional<float> Get() const {
    std::array<float, 2> values = {};
    if (phase_ != Phase::FOUND || !Read(values_, values.data(), sizeof(values)) || !std::isfinite(values[0])) return std::nullopt;
    return values[0];
  }

  // Writes the game- and render-thread values after re-checking the static's link.
  bool Set(float percentage) {
    if (phase_ != Phase::FOUND || !std::isfinite(percentage)) return false;
    std::array<uintptr_t, 2> link = {};
    if (!Read(static_ + 8, link.data(), sizeof(link)) || link[0] != object_ || link[1] != values_) {
      Fail("r.ScreenPercentage moved");
      return false;
    }
    const std::array<float, 2> values = {percentage, percentage};
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(values_), values.data(), sizeof(values), &written)
           && written == sizeof(values);
  }

 private:
  struct Range {
    uintptr_t begin = 0;
    uintptr_t end = 0;
  };
  // {vtable, Target, Ref} in the exe's data.
  struct Static {
    uintptr_t address;
    uintptr_t target;
    uintptr_t ref;
  };

  static constexpr size_t CHUNK = 128 << 10;
  static constexpr size_t MAX_STATICS = 1 << 18;
  static constexpr size_t MAX_SITES = 64;
  static constexpr std::wstring_view HELP = L"To render in lower resolution and upscale for better performance";
  // UTF-16LE L"r.ScreenPercentage" with its terminator.
  static constexpr std::array<char, 38> NAME = [] {
    constexpr char TEXT[] = "r.ScreenPercentage";
    std::array<char, 38> name = {};
    for (size_t index = 0; index < 18; ++index) name[index * 2] = TEXT[index];
    return name;
  }();
  static constexpr size_t NAME_PROBE = 16;  // 'P', rarer than the leading 'r'

  Phase phase_ = Phase::IDLE;
  std::string status_ = "Not started";
  std::string details_;
  Range image_;
  Range functions_;  // RUNTIME_FUNCTION table
  std::vector<Range> readonly_;
  std::vector<Range> writable_;
  std::vector<Range> code_;
  // Chunked scan over `scan_`; buffer_ holds the chunk plus requested context at offset_.
  std::vector<Range> scan_;
  size_t scan_index_ = 0;
  uintptr_t cursor_ = 0;
  uint64_t total_ = 0;
  uint64_t scanned_ = 0;
  std::vector<char> buffer_;
  size_t offset_ = 0;
  std::vector<Static> statics_;  // sorted by address
  size_t checked_ = 0;
  std::vector<Static> by_help_;
  std::vector<uintptr_t> names_;
  std::vector<uintptr_t> sites_;
  uintptr_t static_ = 0;
  uintptr_t object_ = 0;
  uintptr_t values_ = 0;

  void Fail(std::string reason) {
    phase_ = Phase::FAILED;
    status_ = std::move(reason);
  }

  static bool Read(uintptr_t address, void* destination, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size, &read) && read == size;
  }

  static uintptr_t Load(const char* data) {
    uintptr_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
  }

  static bool In(const std::vector<Range>& ranges, uintptr_t address) {
    return std::ranges::any_of(ranges, [&](const Range& range) { return address >= range.begin && address < range.end; });
  }

  std::string Offset(uintptr_t address) const {
    return std::format("exe+0x{:X}", address - image_.begin);
  }

  void Begin(Phase phase, std::vector<Range> ranges) {
    phase_ = phase;
    scan_ = std::move(ranges);
    scan_index_ = 0;
    cursor_ = scan_.empty() ? 0 : scan_[0].begin;
    scanned_ = 0;
    total_ = 0;
    for (const auto& range : scan_) total_ += range.end - range.begin;
  }

  // Reads the next chunk with up to `before`/`after` bytes of context from the same range;
  // unreadable pages read as zeros. False when every range is done.
  bool NextChunk(size_t before, size_t after, uintptr_t* base, size_t* size) {
    while (scan_index_ < scan_.size() && cursor_ >= scan_[scan_index_].end) {
      if (++scan_index_ < scan_.size()) cursor_ = scan_[scan_index_].begin;
    }
    if (scan_index_ >= scan_.size()) return false;
    const auto& range = scan_[scan_index_];
    *base = cursor_;
    *size = std::min<uintptr_t>(CHUNK, range.end - cursor_);
    const uintptr_t begin = cursor_ - std::min<uintptr_t>(before, cursor_ - range.begin);
    const uintptr_t end = std::min<uintptr_t>(range.end, cursor_ + *size + after);
    buffer_.assign(end - begin, 0);
    if (!Read(begin, buffer_.data(), buffer_.size())) {
      for (uintptr_t page = begin; page < end;) {
        const uintptr_t next = std::min<uintptr_t>(end, (page | 0xFFF) + 1);
        Read(page, buffer_.data() + (page - begin), next - page);
        page = next;
      }
    }
    offset_ = cursor_ - begin;
    cursor_ += *size;
    scanned_ += *size;
    return true;
  }

  void StepStatics() {
    uintptr_t base = 0;
    size_t size = 0;
    if (!NextChunk(0, 16, &base, &size)) {
      checked_ = 0;
      phase_ = Phase::HELP;
      return;
    }
    const char* data = buffer_.data();
    for (size_t offset = (8 - base % 8) % 8; offset < size && offset + 24 <= buffer_.size(); offset += 8) {
      const uintptr_t target = Load(data + offset + 8);
      const uintptr_t ref = Load(data + offset + 16);
      if (ref <= target || ref - target > 512 || (target & 7) != 0 || (ref & 3) != 0 || target < 0x10000
          || target >= (uintptr_t{1} << 47) || (target >= image_.begin && target < image_.end)
          || !In(readonly_, Load(data + offset))) continue;
      if (statics_.size() >= MAX_STATICS) {
        Fail("too many console variable candidates");
        return;
      }
      statics_.push_back({.address = base + offset, .target = target, .ref = ref});
    }
  }

  void StepHelp() {
    for (uint32_t count = 0; count < 64 && checked_ < statics_.size(); ++count, ++checked_) {
      const auto& item = statics_[checked_];
      struct {
        uintptr_t data;
        int32_t num;
        int32_t max;
      } help = {};
      std::array<wchar_t, HELP.size()> text = {};
      if (Read(item.target + 8, &help, sizeof(help)) && help.data != 0 && help.num > static_cast<int32_t>(HELP.size())
          && help.num <= help.max && help.max <= (1 << 16) && Read(help.data, text.data(), sizeof(text))
          && std::wstring_view(text.data(), text.size()) == HELP
          && std::ranges::none_of(by_help_, [&](const Static& found) { return found.ref == item.ref; })) {
        by_help_.push_back(item);
      }
    }
    if (checked_ < statics_.size()) return;
    details_ = std::format("help text matched {} of {} console variables", by_help_.size(), statics_.size());
    if (by_help_.size() == 1) {
      Choose(by_help_[0], "help text");
      return;
    }
    Begin(Phase::NAME, readonly_);
  }

  void StepName() {
    uintptr_t base = 0;
    size_t size = 0;
    if (!NextChunk(0, NAME.size() - 1, &base, &size)) {
      if (names_.empty()) {
        details_ += "; name not found";
        Fail("r.ScreenPercentage not found in the game's exe");
        return;
      }
      Begin(Phase::CODE, code_);
      return;
    }
    const char* data = buffer_.data();
    const char* end = data + buffer_.size();
    for (const char* at = data + NAME_PROBE; at < end; ++at) {
      at = static_cast<const char*>(std::memchr(at, NAME[NAME_PROBE], end - at));
      if (at == nullptr) break;
      const size_t position = at - NAME_PROBE - data;
      if (position >= size) break;
      if ((base + position) % 2 == 0 && static_cast<size_t>(end - (data + position)) >= NAME.size()
          && std::memcmp(data + position, NAME.data(), NAME.size()) == 0 && names_.size() < 16) {
        names_.push_back(base + position);
      }
    }
  }

  void StepCode() {
    uintptr_t base = 0;
    size_t size = 0;
    if (!NextChunk(1, 3, &base, &size)) {
      ResolveByName();
      return;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(buffer_.data());
    // RIP-relative operand: ModRM mod 00, r/m 101, then disp32 from the next instruction.
    for (size_t index = std::max<size_t>(offset_, 1); index < offset_ + size && index + 4 <= buffer_.size(); ++index) {
      if ((bytes[index - 1] & 0xC7) != 0x05) continue;
      int32_t displacement = 0;
      std::memcpy(&displacement, bytes + index, 4);
      const uintptr_t address = base + (index - offset_);
      if (std::ranges::find(names_, address + 4 + static_cast<intptr_t>(displacement)) != names_.end() && sites_.size() < MAX_SITES) {
        sites_.push_back(address);
      }
    }
  }

  // Per reference to the name, the static its function constructs: the initializer stores the
  // registered object into Target after loading the name. Ranked by: store to Target, other
  // store or address-of; after the name; distance.
  void ResolveByName() {
    std::vector<RUNTIME_FUNCTION> functions((functions_.end - functions_.begin) / sizeof(RUNTIME_FUNCTION));
    if (!functions.empty() && !Read(functions_.begin, functions.data(), functions.size() * sizeof(RUNTIME_FUNCTION))) functions.clear();
    struct Candidate {
      const Static* item;
      std::array<uintptr_t, 3> rank;
    };
    std::vector<Candidate> candidates;
    for (const uintptr_t site : sites_) {
      const auto section = std::ranges::find_if(code_, [&](const Range& range) { return site >= range.begin && site < range.end; });
      Range window = {.begin = std::max(site - 256, section->begin), .end = std::min(site + 512, section->end)};
      const auto rva = static_cast<DWORD>(site - image_.begin);
      const auto function = std::ranges::upper_bound(functions, rva, {}, &RUNTIME_FUNCTION::BeginAddress);
      if (function != functions.begin() && rva < std::prev(function)->EndAddress) {
        window = {
            .begin = std::max(site - 4096, image_.begin + std::prev(function)->BeginAddress),
            .end = std::min(site + 4096, image_.begin + std::prev(function)->EndAddress),
        };
      }
      std::vector<uint8_t> bytes(window.end - window.begin);
      if (bytes.size() < 8 || !Read(window.begin, bytes.data(), bytes.size())) continue;
      Candidate best = {.item = nullptr, .rank = {UINTPTR_MAX, UINTPTR_MAX, UINTPTR_MAX}};
      for (size_t index = 2; index + 4 <= bytes.size(); ++index) {
        // mov [rip+x], r64 | lea r, [rip+x] | movups/movaps [rip+x], xmm
        const uint8_t opcode = bytes[index - 2];
        if ((bytes[index - 1] & 0xC7) != 0x05 || (opcode != 0x89 && opcode != 0x8D && opcode != 0x11 && opcode != 0x29)) continue;
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + index, 4);
        const uintptr_t address = window.begin + index;
        const uintptr_t operand = address + 4 + static_cast<intptr_t>(displacement);
        if (!In(writable_, operand)) continue;
        for (const uintptr_t member : {operand, operand - 8, operand - 16}) {
          const auto found = std::ranges::lower_bound(statics_, member, {}, &Static::address);
          if (found == statics_.end() || found->address != member) continue;
          const std::array<uintptr_t, 3> rank = {
              (opcode == 0x89 && operand == member + 8) ? 0u : 1u,
              address > site ? 0u : 1u,
              address > site ? address - site : site - address,
          };
          if (rank < best.rank) best = {.item = &*found, .rank = rank};
        }
      }
      if (best.item != nullptr) candidates.push_back(best);
    }
    // The strongest kind of evidence any reference has; distinct variables with it.
    uintptr_t tier = UINTPTR_MAX;
    for (const auto& candidate : candidates) tier = std::min(tier, candidate.rank[0]);
    std::vector<Static> constructed;
    for (const auto& candidate : candidates) {
      if (candidate.rank[0] == tier
          && std::ranges::none_of(constructed, [&](const Static& item) { return item.ref == candidate.item->ref; })) {
        constructed.push_back(*candidate.item);
      }
    }
    details_ += std::format("; name: {} copies", names_.size());
    for (const uintptr_t name : names_) details_ += " " + Offset(name);
    details_ += std::format(", {} code references", sites_.size());
    for (size_t index = 0; index < std::min<size_t>(sites_.size(), 8); ++index) details_ += " " + Offset(sites_[index]);
    details_ += std::format(", {} constructed", constructed.size());
    for (const auto& item : constructed) {
      std::array<float, 2> values = {};
      Read(item.ref, values.data(), sizeof(values));
      details_ += std::format(" [{}: {}, {}]", Offset(item.address), values[0], values[1]);
    }
    if (constructed.size() > 1) {
      // Both strategies together.
      std::erase_if(constructed, [&](const Static& item) {
        return std::ranges::none_of(by_help_, [&](const Static& found) { return found.ref == item.ref; });
      });
    }
    if (constructed.size() != 1) {
      Fail(constructed.empty() ? "r.ScreenPercentage's variable not found" : "r.ScreenPercentage's variable is ambiguous");
      return;
    }
    Choose(constructed[0], "name");
  }

  void Choose(const Static& item, std::string_view how) {
    std::array<float, 2> values = {};
    if (!Read(item.ref, values.data(), sizeof(values)) || !std::isfinite(values[0]) || !std::isfinite(values[1])
        || values[0] < 1.f || values[0] > 400.f || values[1] < 1.f || values[1] > 400.f) {
      details_ += std::format("; {} candidate {} holds {}, {}", how, Offset(item.address), values[0], values[1]);
      Fail("r.ScreenPercentage's value is implausible");
      return;
    }
    static_ = item.address;
    object_ = item.target;
    values_ = item.ref;
    phase_ = Phase::FOUND;
    status_ = "Found";
    details_ += std::format("; found by {} at {}", how, Offset(item.address));
  }
};

}  // namespace expeditionsrome::engine_scale
