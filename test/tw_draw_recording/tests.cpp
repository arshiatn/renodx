// Threaded Draw Recording (twattila/draw_recording.hpp) and the shared worker pool it runs on (twattila/worker_pool.hpp), x86 only.
// Part 1 (C.2-1, S2): the pool: start / stop 100 times, K 2 to 16 to 2 live, the x87 and SSE control words of the starting
// thread seen by every worker on every job, the CPU-set mirror of Keep Render Core Free's published sets (applied once per
// generation), worker thread creation failing (one attempt per target), ALL jobs whose work function claims shared units
// before its own partition, a late worker skipping a closed job, work functions waiting mid-job on a word the owner
// publishes, a job open across game code (every user's Available() is 0, nested and off-owner Starts refused and counted),
// an open job joined at Update, the environment limits (RENODX_TW_WORKER_LIMITS, 1 = the render thread alone) and the spin
// knob, the types' offsets (static asserts in the header).
// Part 2 (C.2-2): the depth path on a synthetic loop with the same state machine as the game's (written from its listing),
// driven by a synthetic driver that splits the list into portions from the tracker's +0x50 (so the portions start at
// begin != 0, back to back on one tracker, and a stale stride changes the split). The fake flush does the real flush's
// writes (+0x5c, +0x28, the +0x69 byte it reads and clears) and logs the +0x69 byte it saw. The serial call log equals the log
// of the Chunked, Recorded, One Worker and Workers stages at K = 2 to 16 over lists with runs of 1 to 300 items, animation
// -1 runs, meshes without a technique before cuts and in the last chunk, tiny op buffers (overflow: run again), two passes in a
// row on one tracker, calls with 200000 items; Touch-Ahead; the fall-backs (entry state, continuation of foreign mesh state);
// the flush entry site; the Record rules (group, any byte); the trace's in-frame compare and its time accounting. Controls:
// each safeguard off (seed validation, pointer translation, continuation guard, any capture, the stride rule), a dropped
// flush, a wrong animation stack slot, an op injected into one chunk: every one must make the run differ.
// Part 3 (C.2-3): the colour path on a second synthetic loop (same machine plus the variation path, the constants, the
// textures, the faction block and a vtable check at the mesh setup), meshes that recur in several chunks (the owner table: the
// call runs directly), meshes of an unknown class, constants that are NaN / -0 / equal in a row with the game's cache flag on
// and off, every faction case, an "any" byte left over at a chunk's end, and the colour controls (snapshot restore off, the
// owner table off with another chunk's write racing in, seed validation off): each must make the run differ.
// Part 3b (C.2-3 review items): a continuation or warm-up item of an unknown class, the call-start claim, the warm-up's mesh
// restore (strict flush) and the constant repeat counts, each with a control that must differ; the trace's seed misses by field
// and constant repeats; a count of recorded continuations.
// Part 4 (C.2-5): the B groups (Depth B, Colour B) on synthetic mesh bodies with the same state machines (no animation, the
// flush through the entry hook, the class's virtual call at a batch start, a flag byte carried across meshes, the colour body's
// state block) behind a per-mesh dispatcher and the 23-class table: every stage at K 2 to 16 over lists with direct classes
// between census chunks, empty slices, long meshes, meshes without a technique; the view types, the fall-backs and the family
// bits; the entry check (a count, a mask or a batch size left by a direct segment), the seeds (warm-up step back, unknown seed),
// the stride rule, overflow, a mesh twice in a round, the latch; the VF64 and STATE ops. Controls: class flags, entry check,
// seed validation, stride rule, any capture, snapshot restore, a dropped end flush and an injected op must each show.
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

#include "src/games/twattila/draw_recording.hpp"

namespace dr = twattila::draw_recording;
namespace cs = twattila::cpu_sets;
namespace di = twattila::draw_recording::internal;
namespace wp = twattila::worker_pool;
namespace wi = twattila::worker_pool::internal;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

// One present of the add-on: the pool's row (`threads`, the render thread included), then the user's own Update.
static void UpdateAt(uint32_t threads, const dr::Settings& settings) {
  wp::Update({.threads = threads, .in_use = settings.stage != dr::Stage::OFF});
  dr::Update(settings);
}

static uint32_t ProcessThreadCount() {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  uint32_t count = 0;
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
  }
  CloseHandle(snapshot);
  return count;
}

// ---------------------------------------------------------------- part 1: the shared worker pool (worker_pool.hpp)

struct WorkerReport {
  std::atomic<uint32_t> runs{0};
  uint32_t x87 = 0;
  uint32_t sse = 0;
  uint32_t mxcsr = 0;
  DWORD thread_id = 0;
};
// By worker number (1 to 15; the render thread is 0).
static std::array<WorkerReport, wp::MAX_WORKERS + 1> reports;

static void ReportWork(void*, uint32_t worker) {
  WorkerReport& report = reports[worker];
  unsigned int x87 = 0;
  unsigned int sse = 0;
  __control87_2(0, 0, &x87, &sse);
  report.x87 = x87;
  report.sse = sse;
  report.mxcsr = _mm_getcsr();
  report.thread_id = GetCurrentThreadId();
  // Game code on a worker may leave its own control words changed: the next dispatch must set them again.
  unsigned int ignored = 0;
  __control87_2(_PC_24 | _RC_CHOP, _MCW_RC | _MCW_PC, &ignored, nullptr);
  __control87_2(_RC_CHOP | _DN_FLUSH, _MCW_RC | _MCW_DN, nullptr, &ignored);
  report.runs.fetch_add(1);
}

static void RunReport(uint32_t workers) {
  wp::Run({.work = ReportWork, .join = wp::JoinRule::ALL}, workers, [] {});
}

static void ResetReports() {
  for (WorkerReport& report : reports) report.runs = 0;
}

static void PoolStartStop() {
  const uint32_t baseline = ProcessThreadCount();
  for (int cycle = 0; cycle < 100; ++cycle) {
    wp::Update({.threads = 16, .in_use = true});
    REQUIRE(wi::created == 15);
    ResetReports();
    RunReport(15);
    std::array<DWORD, 15> ids = {};
    for (uint32_t w = 1; w <= 15; ++w) {
      CHECK(reports[w].runs == 1);
      ids[w - 1] = reports[w].thread_id;
      CHECK(ids[w - 1] != GetCurrentThreadId());
    }
    std::ranges::sort(ids);
    CHECK(std::ranges::adjacent_find(ids) == ids.end());
    CHECK(ProcessThreadCount() == baseline + 15);
    wp::Update({.in_use = false});
    CHECK(wi::created == 0 && wp::Available(wp::User::DRAW_RECORDING) == 0);
    CHECK(ProcessThreadCount() == baseline);
  }
  std::printf("pool: 100 start / stop cycles, 15 distinct threads each, no thread left\n");
}

static void PoolLiveK() {
  const std::array<uint32_t, 11> sequence = {2, 4, 6, 8, 12, 16, 12, 8, 6, 4, 2};
  uint32_t most = 0;
  for (const uint32_t k : sequence) {
    wp::Update({.threads = k, .in_use = true});
    most = std::max(most, k - 1);
    // Never shrinks while in use: the extra threads sleep and are not offered.
    CHECK(wi::created == most);
    CHECK(wp::Available(wp::User::DRAW_RECORDING) == k - 1);
    ResetReports();
    RunReport(k - 1);
    for (uint32_t w = 1; w <= wp::MAX_WORKERS; ++w) CHECK(reports[w].runs == (w <= k - 1 ? 1u : 0u));
    // More workers than the row offers: refused, nothing started.
    CHECK(!wp::Start({.work = ReportWork}, k));
  }
  wp::Update({.in_use = false});
  std::printf("pool: K 2 to 16 to 2 live: only the first K - 1 workers ran each time\n");
}

static void PoolControlWords() {
  wp::Update({.threads = 16, .in_use = true});
  unsigned int saved_x87 = 0;
  unsigned int saved_sse = 0;
  __control87_2(0, 0, &saved_x87, &saved_sse);
  const unsigned int x87_mask = _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC;
  const unsigned int x87_values[] = {_PC_24 | _RC_UP, _PC_53 | _RC_CHOP, _PC_64 | _RC_DOWN, _PC_24 | _RC_NEAR};
  const unsigned int rounding[] = {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP};
  const unsigned int denormals[] = {_DN_SAVE, _DN_FLUSH, _DN_FLUSH_OPERANDS_SAVE_RESULTS, _DN_SAVE_OPERANDS_FLUSH_RESULTS};
  int checked = 0;
  for (const unsigned int x87_value : x87_values) {
    for (const unsigned int round : rounding) {
      for (const unsigned int denormal : denormals) {
        unsigned int ignored = 0;
        __control87_2(x87_value, x87_mask, &ignored, nullptr);
        __control87_2(round | denormal, _MCW_RC | _MCW_DN, nullptr, &ignored);
        const uint32_t mine = _mm_getcsr();
        unsigned int my_x87 = 0;
        __control87_2(0, 0, &my_x87, &ignored);
        // Back to back on the same workers, each of which clobbered its own words after the last dispatch.
        RunReport(15);
        for (uint32_t w = 1; w <= 15; ++w) {
          CHECK(((reports[w].mxcsr ^ mine) & 0xffc0) == 0);
          CHECK(((reports[w].x87 ^ my_x87) & x87_mask) == 0);
        }
        ++checked;
      }
    }
  }
  __control87_2(saved_x87, x87_mask, &saved_x87, nullptr);
  __control87_2(saved_sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &saved_sse);
  wp::Update({.in_use = false});
  std::printf("pool: x87 PC / RC and MXCSR RC / DAZ / FTZ copied to all 15 workers on every dispatch (%d combinations)\n", checked);
}

// The CPU sets the pool threads were given follow cpu_sets' published generation, applied once per generation.
static void PoolCpuSets() {
  const std::vector<cs::CpuSet> system = cs::SystemCpuSets();
  if (system.size() < 4) {
    std::printf("pool: CPU-set mirror skipped (fewer than 4 CPU sets)\n");
    return;
  }
  std::vector<ULONG> sets = {system[2].id, system[3].id};
  std::ranges::sort(sets);
  wp::Update({.threads = 8, .in_use = true});
  const auto publish = [](const std::vector<ULONG>& published_sets) {
    const std::unique_lock lock(cs::published.mutex);
    cs::published.worker_sets = published_sets;
    cs::published.generation.fetch_add(1, std::memory_order_release);
  };
  const auto all_equal = [](const std::vector<ULONG>& expected) {
    for (uint32_t w = 0; w < wi::created; ++w) {
      if (cs::ReadSets(GetThreadSelectedCpuSets, wi::pool[w].thread) != expected) return false;
    }
    return true;
  };
  CHECK(all_equal({}));
  publish(sets);
  wp::Update({.threads = 8, .in_use = true});
  CHECK(all_equal(sets));
  // A thread started later is given the sets too.
  wp::Update({.threads = 12, .in_use = true});
  CHECK(wi::created == 11);
  CHECK(all_equal(sets));
  // The sets are read once per generation: a change made behind the pool's back stays until the next publish.
  SetThreadSelectedCpuSets(wi::pool[0].thread, nullptr, 0);
  wp::Update({.threads = 12, .in_use = true});
  CHECK(cs::ReadSets(GetThreadSelectedCpuSets, wi::pool[0].thread) == std::vector<ULONG>{});
  CHECK(wi::given_generation == cs::AppliedGeneration());
  publish({});
  wp::Update({.threads = 12, .in_use = true});
  CHECK(all_equal({}));
  wp::Update({.in_use = false});
  std::printf("pool: CPU-set mirror: the published sets are the pool threads' sets (set, grown, cleared), applied once per generation\n");
}

// A worker thread that can't be created is tried once per target (and per Stop), not every present.
static void PoolThreadFailure() {
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
  wp::Update({.in_use = false});
  CHECK(wi::created == 0);
  wi::controls.fail_thread_creation = true;
  wi::thread_attempts = 0;
  for (int present = 0; present < 5; ++present) wp::Update({.threads = 8, .in_use = true});
  CHECK(wi::thread_attempts == 1);
  CHECK(wi::created == 0 && wp::Available(wp::User::DRAW_RECORDING) == 0);
  wp::Update({.threads = 6, .in_use = true});
  CHECK(wi::thread_attempts == 2);
  const uint32_t attempts = wi::thread_attempts;
  wi::controls.fail_thread_creation = false;
  // Same target: still not tried.
  wp::Update({.threads = 6, .in_use = true});
  CHECK(wi::thread_attempts == 2 && wi::created == 0);
  // The target changed: tried again, and now it works.
  wp::Update({.threads = 7, .in_use = true});
  CHECK(wi::created == 6 && wp::Available(wp::User::DRAW_RECORDING) == 6);
  wp::Update({.in_use = false});
  CHECK(wi::created == 0);
  // After a Stop the memory of the failure is gone too.
  wi::controls.fail_thread_creation = true;
  wp::Update({.threads = 7, .in_use = true});
  wp::Update({.in_use = false});
  wi::controls.fail_thread_creation = false;
  wp::Update({.threads = 7, .in_use = true});
  CHECK(wi::created == 6);
  wp::Update({.in_use = false});
  std::printf("pool: a failed CreateThread is tried once per target (%u attempts for 7 presents)\n", attempts);
#else
  std::printf("pool: thread failure skipped (TWATTILA_WORKER_POOL_CONTROLS not defined)\n");
#endif
}

// Shared units claimed from a counter, then the worker's own partition (property (c)): every unit once, every woken worker ran.
struct Claims {
  std::atomic<uint32_t> next{0};
  uint32_t units = 0;
  std::array<std::atomic<uint32_t>, 4096> hits = {};
  std::array<std::atomic<uint32_t>, wp::MAX_WORKERS + 1> by_thread = {};
  std::array<std::atomic<uint32_t>, wp::MAX_WORKERS + 1> partition = {};
};

static void ClaimWork(void* context, uint32_t worker) {
  auto& claims = *static_cast<Claims*>(context);
  for (uint32_t unit = claims.next.fetch_add(1); unit < claims.units; unit = claims.next.fetch_add(1)) {
    claims.hits[unit].fetch_add(1);
    claims.by_thread[worker].fetch_add(1);
  }
  claims.partition[worker].fetch_add(1);
}

static void ResetClaims(Claims* claims, uint32_t units) {
  claims->next = 0;
  claims->units = units;
  for (auto& hit : claims->hits) hit = 0;
  for (auto& count : claims->by_thread) count = 0;
  for (auto& count : claims->partition) count = 0;
}

static bool EveryUnitOnce(const Claims& claims) {
  for (uint32_t unit = 0; unit < claims.units; ++unit) {
    if (claims.hits[unit] != 1) return false;
  }
  return true;
}

// Property (c): an ALL job's work function claims shared units before its own partition. The owner may drain the units alone;
// every woken worker still runs.
static void PoolAllClaims() {
  wp::Update({.threads = 6, .in_use = true});
  const auto claims = std::make_unique<Claims>();
  for (int round = 0; round < 300; ++round) {
    ResetClaims(claims.get(), 2000);
    const wp::JoinResult joined = wp::Run({.work = ClaimWork, .context = claims.get(), .user = wp::User::DRAW_RECORDING,
                                           .join = wp::JoinRule::ALL},
                                          5, [&] { ClaimWork(claims.get(), 0); });
    CHECK(joined.ran == 5);
    CHECK(EveryUnitOnce(*claims));
    for (uint32_t w = 0; w <= 5; ++w) CHECK(claims->partition[w] == 1);
  }
  wp::Update({.in_use = false});
  std::printf("pool: ALL job: every woken worker ran its partition, shared units claimed once (300 rounds)\n");
}

// ENTERED: the owner claims until none is left and Join waits only for workers already inside; a worker that wakes late finds
// the job closed and skips it, and never touches the memory after Join.
static void PoolLateWorker() {
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
  wp::Update({.threads = 4, .in_use = true});
  const auto claims = std::make_unique<Claims>();
  ResetClaims(claims.get(), 1000);
  wi::controls.late_worker = 2;
  wi::controls.late_parked = 0;
  wi::controls.release_late = false;
  const wp::JoinResult joined = wp::Run(
      {.work = ClaimWork, .context = claims.get(), .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ENTERED}, 2,
      [&] { ClaimWork(claims.get(), 0); });
  // Join returned although worker 2 had not counted itself in.
  CHECK(joined.ran <= 1);
  CHECK(EveryUnitOnce(*claims));
  for (int wait = 0; wait < 2000 && wi::controls.late_parked == 0; ++wait) Sleep(1);
  CHECK(wi::controls.late_parked == 1);
  // The memory is a new job's now: the late worker must not claim from it.
  ResetClaims(claims.get(), 1000);
  wi::controls.release_late = true;
  for (int wait = 0; wait < 2000 && wi::entered.load() != 0; ++wait) Sleep(1);
  Sleep(20);
  CHECK(wi::entered.load() == 0);
  CHECK(claims->next == 0 && claims->by_thread[2] == 0 && claims->partition[2] == 0);
  // The late worker is fine for the next job.
  wi::controls.late_worker = 0;
  ResetReports();
  RunReport(2);
  CHECK(reports[1].runs == 1 && reports[2].runs == 1);
  wp::Update({.in_use = false});
  std::printf("pool: a late worker skips a closed job and never touches its memory; the next job runs it\n");
#else
  std::printf("pool: late worker skipped (TWATTILA_WORKER_POOL_CONTROLS not defined)\n");
#endif
}

// A word the owner publishes between Start and Join, which the work function waits on (property (a)).
struct Gate {
  std::atomic<uint32_t> word{0};
  std::atomic<uint32_t> passed{0};
};

static void GatedWork(void* context, uint32_t) {
  auto& gate = *static_cast<Gate*>(context);
  for (int spin = 0; spin < 2000 && gate.word.load(std::memory_order_acquire) == 0; ++spin) _mm_pause();
  while (gate.word.load(std::memory_order_acquire) == 0) gate.word.wait(0);
  gate.passed.fetch_add(1);
}

static void PoolMidJobWait() {
  wp::Update({.threads = 5, .in_use = true});
  for (const wp::JoinRule rule : {wp::JoinRule::ALL, wp::JoinRule::ENTERED}) {
    Gate gate;
    REQUIRE(wp::Start({.work = GatedWork, .context = &gate, .user = wp::User::ANIMATION_ROWS, .join = rule}, 4));
    Sleep(30);
    CHECK(gate.passed == 0);
    gate.word.store(1, std::memory_order_release);
    gate.word.notify_all();
    const wp::JoinResult joined = wp::Join();
    CHECK(gate.passed == 4 && joined.ran == 4);
  }
  wp::Update({.in_use = false});
  std::printf("pool: workers waiting mid-job on a word published between Start and Join (ALL and ENTERED) all come out\n");
}

static size_t LogCount(std::string_view text) {
  const std::unique_lock lock(log_mutex);
  return static_cast<size_t>(std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

static bool LogHas(std::string_view text) { return LogCount(text) != 0; }

// A job open across game code on the owner thread (property (b)), nested Starts, other threads, the trace's refusal counts, and
// the trace record: Join logs nothing, the next Update prints it.
static void PoolRefusals() {
  wp::Update({.threads = 4, .in_use = true, .trace = true});
  const auto users = [] { return static_cast<size_t>(wp::User::COUNT); };
  const auto count_of = [](wp::User user) { return wi::refused[static_cast<size_t>(user)].load(); };
  for (auto& count : wi::refused) count = 0;
  const auto claims = std::make_unique<Claims>();
  ResetClaims(claims.get(), 1000);
  for (size_t user = 0; user < users(); ++user) CHECK(wp::Available(static_cast<wp::User>(user)) == 3);
  // Off the owner thread: no workers (counted), Start refused (counted), Join does nothing.
  std::thread other([&] {
    for (size_t user = 0; user < users(); ++user) CHECK(wp::Available(static_cast<wp::User>(user)) == 0);
    CHECK(!wp::Start({.work = ClaimWork, .context = claims.get(), .user = wp::User::PENDING_SWEEP, .join = wp::JoinRule::ENTERED}, 1));
    CHECK(wp::Join().wait_ticks == 0);
  });
  other.join();
  CHECK(count_of(wp::User::PENDING_SWEEP) == 2 && count_of(wp::User::RING_FILL) == 1 && !wi::busy.load());
  for (auto& count : wi::refused) count = 0;
  // The effect update's window: Start, game code, Join.
  REQUIRE(wp::Start({.work = ClaimWork, .context = claims.get(), .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ENTERED}, 2));
  for (size_t user = 0; user < users(); ++user) CHECK(wp::Available(static_cast<wp::User>(user)) == 0);
  // A nested Start is refused; a nested Run runs its part alone and joins nothing.
  CHECK(!wp::Start({.work = ReportWork, .user = wp::User::RING_FILL, .join = wp::JoinRule::ENTERED}, 1));
  uint32_t alone = 0;
  const wp::JoinResult nested = wp::Run({.work = ReportWork, .user = wp::User::RING_FILL, .join = wp::JoinRule::ENTERED}, 1,
                                        [&] { ++alone; });
  CHECK(alone == 1 && nested.ran == 0 && nested.wait_ticks == 0);
  CHECK(count_of(wp::User::RING_FILL) == 3 && count_of(wp::User::EFFECT_UPDATE) == 1);
  // Another thread's Join does not close the owner's job.
  std::thread other_join([] { CHECK(wp::Join().wait_ticks == 0); });
  other_join.join();
  CHECK(wi::busy.load());
  ClaimWork(claims.get(), 0);
  wp::Join();
  CHECK(!wi::busy.load() && EveryUnitOnce(*claims));
  for (size_t user = 0; user < users(); ++user) CHECK(wp::Available(static_cast<wp::User>(user)) == 3);
  CHECK(count_of(wp::User::RING_FILL) == 3);
  // A trace job: Join keeps a record and logs nothing; the next Update prints it and the refusals, then the counts start again.
  ResetClaims(claims.get(), 1000);
  const size_t lines = LogCount("Worker pool trace");
  wp::Run({.work = ClaimWork, .context = claims.get(), .user = wp::User::ANIMATION_ROWS, .join = wp::JoinRule::ALL, .trace = true}, 2,
          [&] { ClaimWork(claims.get(), 0); });
  CHECK(LogCount("Worker pool trace") == lines && wi::record_count == 1);
  wp::Update({.threads = 4, .in_use = true, .trace = true});
  CHECK(LogCount("Worker pool trace (animation)") == 1 && LogHas("worker 1 on CPU") && LogHas("worker 2 on CPU"));
  CHECK(LogHas("ring 3") && LogHas("effects 1"));
  CHECK(wi::record_count == 0 && wi::record_drops == 0);
  for (const auto& count : wi::refused) CHECK(count == 0);
  // Without a trace frame nothing is counted.
  wp::Update({.threads = 4, .in_use = true});
  std::thread untraced([] { CHECK(wp::Available(wp::User::RING_FILL) == 0); });
  untraced.join();
  CHECK(count_of(wp::User::RING_FILL) == 0);
  wp::Update({.in_use = false});
  std::printf("pool: a job open across game code gives every user 0; nested and off-owner calls refused and counted while tracing; Join logs nothing, Update prints the record\n");
}

// A refused ALL job (a job already open) still completes: the workers' parts run on the owner after its own.
static void PoolRefusedAllCompletes() {
  wp::Update({.threads = 4, .in_use = true});
  Gate gate;
  gate.word = 1;
  REQUIRE(wp::Start({.work = GatedWork, .context = &gate, .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ENTERED}, 1));
  ResetReports();
  uint32_t own_part = 0;
  wp::Run({.work = ReportWork, .user = wp::User::DRAW_RECORDING, .join = wp::JoinRule::ALL}, 3, [&] { ++own_part; });
  CHECK(own_part == 1);
  for (uint32_t w = 1; w <= 3; ++w) CHECK(reports[w].runs == 1 && reports[w].thread_id == GetCurrentThreadId());
  wp::Join();
  wp::Update({.in_use = false});
  std::printf("pool: an ALL job whose Start was refused runs the workers' parts on the owner\n");
}

// Stop from another thread while the owner's job is open keeps the pool and pins the module; the owner's Join finishes the job.
static void PoolStopWithOpenJob() {
  wp::Update({.threads = 4, .in_use = true});
  Gate gate;
  REQUIRE(wp::Start({.work = GatedWork, .context = &gate, .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ALL}, 3));
  std::thread other([] { wp::Stop(); });
  other.join();
  CHECK(wi::created == 3 && wi::pinned && LogHas("the module is pinned"));
  // A second Stop says nothing more; the job still completes.
  gate.word.store(1, std::memory_order_release);
  gate.word.notify_all();
  const wp::JoinResult joined = wp::Join();
  CHECK(joined.ran == 3 && gate.passed == 3 && !wi::busy.load());
  wp::Stop();
  CHECK(wi::created == 0 && !wi::busy.load());
  std::printf("pool: Stop with a job open on another thread keeps the pool, pins the module; the owner's Join completes the job\n");
}

// Stop on another thread holds `busy` while it waits for a worker that is slow to leave. The owner's Update, Join and Stop must not
// release it (a stale starter from an earlier job), must not grow the pool, and the threads end exactly once.
static void PoolStopHoldsBusy() {
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
  const uint32_t baseline = ProcessThreadCount();
  wp::Update({.threads = 4, .in_use = true});
  // A job that ran on this thread before: its thread must not stay as the starter.
  RunReport(3);
  CHECK(wi::created == 3 && wi::starter.load() == 0 && ProcessThreadCount() == baseline + 3);
  wi::controls.hold_exit = true;
  std::thread stopper([] { wp::Stop(); });
  for (int wait = 0; wait < 2000 && !wi::stopping.load(); ++wait) Sleep(1);
  REQUIRE(wi::stopping.load() && wi::busy.load());
  wp::Update({.threads = 16, .in_use = true});
  CHECK(wi::busy.load() && wi::stopping.load() && wi::created == 3);
  CHECK(wp::Join().wait_ticks == 0 && wi::busy.load());
  wp::Stop();
  CHECK(wi::busy.load() && wi::created == 3);
  CHECK(wp::Available(wp::User::DRAW_RECORDING) == 0);
  CHECK(LogHas("a job is open on another thread"));
  wi::controls.hold_exit = false;
  stopper.join();
  CHECK(wi::created == 0 && !wi::busy.load() && !wi::stopping.load() && wi::pool[0].thread == nullptr);
  CHECK(ProcessThreadCount() == baseline);
  // The pool starts again afterwards.
  wp::Update({.threads = 3, .in_use = true});
  CHECK(wi::created == 2 && wp::Available(wp::User::DRAW_RECORDING) == 2);
  wp::Update({.in_use = false});
  std::printf("pool: Stop on another thread keeps busy through the owner's Update / Join / Stop; no growth meanwhile, threads end once\n");
#else
  std::printf("pool: Stop holding busy skipped (TWATTILA_WORKER_POOL_CONTROLS not defined)\n");
#endif
}

// ENTERED stress: short jobs at K 2 to 16 where the owner drains the units alone and flags the context right after Join; a work
// function that sees the flag at its start or end ran past the Join. It tests the job logic and the generation handling (a late
// worker skipping, the contexts reused every 64 jobs), not the memory ordering: no x86 test can show a weaker order.
struct StressJob {
  std::atomic<uint32_t> next{0};
  std::atomic<bool> joined{false};
};
static std::atomic<uint32_t> stress_failures{0};

static void StressWork(void* context, uint32_t) {
  auto& job = *static_cast<StressJob*>(context);
  if (job.joined.load()) stress_failures.fetch_add(1);
  for (uint32_t unit = job.next.fetch_add(1); unit < 8; unit = job.next.fetch_add(1)) {
  }
  if (job.joined.load()) stress_failures.fetch_add(1);
}

// Fails the run if the test does not finish in time (a lost wake-up would hang it).
struct Watchdog {
  std::atomic<bool> finished{false};
  std::thread thread;
  Watchdog(int seconds, const char* what) : thread([this, seconds, what] {
    for (int tick = 0; tick < seconds * 10 && !finished.load(); ++tick) Sleep(100);
    if (!finished.load()) {
      std::printf("FAIL watchdog: %s did not finish in %d s\n", what, seconds);
      std::exit(1);
    }
  }) {}
  ~Watchdog() {
    finished = true;
    thread.join();
  }
};

static void PoolEnteredStress() {
  constexpr uint32_t JOBS_PER_K = 14000;
  const Watchdog watchdog(120, "ENTERED stress");
  const auto jobs = std::make_unique<std::array<StressJob, 64>>();
  stress_failures = 0;
  uint32_t total = 0;
  for (uint32_t k = 2; k <= 16; ++k) {
    wp::Update({.threads = k, .in_use = true});
    for (uint32_t n = 0; n < JOBS_PER_K; ++n) {
      StressJob& job = (*jobs)[n % jobs->size()];
      job.next = 0;
      job.joined = false;
      wp::Run({.work = StressWork, .context = &job, .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ENTERED}, k - 1,
              [&] { StressWork(&job, 0); });
      job.joined = true;
      ++total;
    }
  }
  wp::Update({.in_use = false});
  CHECK(stress_failures == 0);
  std::printf("pool: ENTERED stress: %u short jobs at K 2..16, no work function ran past its Join\n", total);
}

// Work that takes longer than the join's spin: the last finisher's wake-up of Join must arrive (both rules), under a watchdog.
struct SlowJob {
  std::atomic<uint32_t> ran{0};
};

static void SlowWork(void* context, uint32_t) {
  Sleep(20);
  static_cast<SlowJob*>(context)->ran.fetch_add(1);
}

static void PoolSlowWork() {
  wp::Update({.threads = 4, .in_use = true});
  const Watchdog watchdog(10, "slow work");
  for (const wp::JoinRule rule : {wp::JoinRule::ALL, wp::JoinRule::ENTERED}) {
    SlowJob slow;
    // ENTERED: the owner has no units; it waits until the workers are inside, so Join has to wait for them.
    const wp::JoinResult joined = wp::Run({.work = SlowWork, .context = &slow, .user = wp::User::ANIMATION_ROWS, .join = rule}, 3,
                                          [&] { Sleep(5); });
    CHECK(slow.ran == 3 && joined.ran == 3);
    CHECK(joined.wait_ticks * 1000 / wp::qpc_frequency >= 10);
  }
  wp::Update({.in_use = false});
  std::printf("pool: work slower than the join's spin (20 ms), both rules: Join is woken\n");
}

// An update with a job still open joins it, and says so once.
static void PoolOpenAtUpdate() {
  wp::Update({.threads = 4, .in_use = true});
  wi::open_logged = false;
  ResetReports();
  for (int round = 0; round < 2; ++round) {
    REQUIRE(wp::Start({.work = ReportWork, .user = wp::User::DRAW_RECORDING, .join = wp::JoinRule::ALL}, 3));
    wp::Update({.threads = 4, .in_use = true});
    CHECK(!wi::busy.load() && wi::open.load() == 0 && wi::entered.load() == 0);
    CHECK(reports[1].runs == round + 1u && reports[2].runs == round + 1u && reports[3].runs == round + 1u);
    CHECK(wp::Available(wp::User::DRAW_RECORDING) == 3);
  }
  CHECK(LogCount("a job was still open at the present") == 1);
  wp::Update({.in_use = false});
  std::printf("pool: a job open at Update is joined, logged once\n");
}

// RENODX_TW_WORKER_LIMITS and RENODX_TW_POOL_SPIN_US: read at the first present and again at a new env generation, with the pool
// running and no job open; a limit of 1 is the render thread alone; one line says what was applied; a bad entry is said once.
static void PoolEnvironment() {
  wp::Update({.in_use = false});
  uint32_t generation = 0;
  const auto set_env = [](const char* limits, const char* spin) {
    SetEnvironmentVariableA("RENODX_TW_WORKER_LIMITS", limits);
    SetEnvironmentVariableA("RENODX_TW_POOL_SPIN_US", spin);
  };
  const auto update = [&](uint32_t threads) { wp::Update({.threads = threads, .in_use = true, .env_generation = generation}); };
  const auto ticks_of = [](uint64_t microseconds) { return static_cast<uint32_t>(wp::qpc_frequency * microseconds / 1000000); };
  set_env("recording:1,effects:3,ring:99,bogus:2,animation:x,sweep,sweep:4q", "50");
  generation = 1;
  update(8);
  CHECK(wp::Available(wp::User::DRAW_RECORDING) == 0);
  CHECK(wp::Available(wp::User::EFFECT_UPDATE) == 2);
  CHECK(wp::Available(wp::User::ANIMATION_ROWS) == 7);
  CHECK(wp::Available(wp::User::PENDING_SWEEP) == 7);
  CHECK(wp::Available(wp::User::RING_FILL) == 7);
  CHECK(wi::spin_ticks.load() == ticks_of(50));
  // The applied line: the values actually used (the ring limit clamped to 16, the entries that did not parse left at 16).
  CHECK(LogHas("limits and spin applied (env generation 1): recording 1 effects 3 animation 16 sweep 16 ring 16, spin 50 us."));
  for (const char* entry : {"\"bogus:2\"", "\"animation:x\"", "\"sweep\"", "\"sweep:4q\""}) {
    CHECK(LogCount(std::string("RENODX_TW_WORKER_LIMITS entry ") + entry + " does not parse") == 1);
  }
  // The limit and the row: the smaller wins.
  update(2);
  CHECK(wp::Available(wp::User::EFFECT_UPDATE) == 1 && wp::Available(wp::User::RING_FILL) == 1);
  update(8);
  // Same generation: a change of the variables is not seen.
  set_env("recording:6", nullptr);
  update(8);
  CHECK(wp::Available(wp::User::DRAW_RECORDING) == 0 && wi::spin_ticks.load() == ticks_of(50));
  // A new generation, the pool running: read again.
  CHECK(wi::created == 7);
  generation = 2;
  update(8);
  CHECK(wi::created == 7 && wp::Available(wp::User::DRAW_RECORDING) == 5 && wp::Available(wp::User::EFFECT_UPDATE) == 7);
  CHECK(wi::spin_ticks.load() == ticks_of(wi::DEFAULT_SPIN_US));
  CHECK(LogHas("(env generation 2): recording 6 effects 16 animation 16 sweep 16 ring 16, spin 3 us."));
  // 1 is allowed and the work still runs, on the render thread alone.
  set_env("recording:1", nullptr);
  generation = 3;
  update(8);
  ResetReports();
  RunReport(wp::Available(wp::User::DRAW_RECORDING));
  CHECK(!wp::Start({.work = ReportWork, .user = wp::User::DRAW_RECORDING}, wp::Available(wp::User::DRAW_RECORDING)));
  for (const WorkerReport& report : reports) CHECK(report.runs == 0);
  // The same bad entries again, and a bad spin: each said once in all.
  set_env("bogus:2,sweep", "fast");
  generation = 4;
  update(8);
  generation = 5;
  update(8);
  CHECK(LogCount("RENODX_TW_WORKER_LIMITS entry \"bogus:2\" does not parse") == 1);
  CHECK(LogCount("RENODX_TW_WORKER_LIMITS entry \"sweep\" does not parse") == 1);
  CHECK(LogCount("RENODX_TW_POOL_SPIN_US=fast does not parse") == 1);
  CHECK(wi::spin_ticks.load() == ticks_of(wi::DEFAULT_SPIN_US));
  // A job open when the generation changes is joined first; the read then happens.
  Gate gate;
  gate.word = 1;
  set_env("ring:2", nullptr);
  generation = 6;
  REQUIRE(wp::Start({.work = GatedWork, .context = &gate, .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ENTERED}, 1));
  update(8);
  CHECK(!wi::busy.load() && wp::Available(wp::User::RING_FILL) == 1 && wp::Available(wp::User::EFFECT_UPDATE) == 7);
  // No tear while idle workers spin: the spin is one 32-bit word, changed between two values at every present.
  const uint32_t low = ticks_of(3);
  const uint32_t high = ticks_of(1000000);
  std::atomic<bool> sampling{true};
  std::atomic<uint32_t> torn{0};
  std::thread sampler([&] {
    while (sampling.load()) {
      const uint32_t seen = wi::spin_ticks.load();
      if (seen != low && seen != high) torn.fetch_add(1);
    }
  });
  for (int round = 0; round < 1500; ++round) {
    set_env(nullptr, (round % 2 == 0 ? "1000000" : "3"));
    ++generation;
    update(8);
    RunReport(7);
  }
  sampling = false;
  sampler.join();
  CHECK(torn == 0);
  // Back to the defaults at generation 0, the one the other tests run with.
  set_env(nullptr, nullptr);
  generation = 0;
  update(8);
  for (size_t user = 0; user < static_cast<size_t>(wp::User::COUNT); ++user) CHECK(wp::Available(static_cast<wp::User>(user)) == 7);
  CHECK(wi::spin_ticks.load() == ticks_of(wi::DEFAULT_SPIN_US));
  wp::Update({.in_use = false});
  std::printf("pool: env limits and spin read again at a new generation with the pool running, the applied line, bad entries said once, no tear of the spin\n");
}

// ArmTrace: a header line at the next present (time and the pool's values), refusals counted and Tracing() true for the window.
static void PoolArmTrace() {
  wp::Update({.threads = 4, .in_use = true});
  const size_t headers = LogCount("Worker pool trace window");
  for (auto& count : wi::refused) count = 0;
  CHECK(!wp::Tracing());
  wp::ArmTrace(2);
  CHECK(wp::Tracing());
  const auto refused_ring = [] {
    std::thread other([] { wp::Available(wp::User::RING_FILL); });
    other.join();
    return wi::refused[static_cast<size_t>(wp::User::RING_FILL)].load();
  };
  wp::Update({.threads = 4, .in_use = true});
  CHECK(LogCount("Worker pool trace window") == headers + 1 && LogHas("window of 2 presents") && LogHas("workers ready 3 of 3"));
  CHECK(LogHas("spin 3 us, env generation 0, limits recording 16 effects 16"));
  CHECK(wp::Tracing() && refused_ring() == 1);
  wp::Update({.threads = 4, .in_use = true});
  CHECK(wp::Tracing() && LogHas("ring 1"));
  wp::Update({.threads = 4, .in_use = true});
  CHECK(!wp::Tracing() && LogCount("Worker pool trace window") == headers + 1);
  CHECK(refused_ring() == 0);
  wp::Update({.in_use = false});
  std::printf("pool: ArmTrace: one header line, counting and Tracing() for the window, then off\n");
}

// ---------------------------------------------------------------- part 2: the synthetic game

template <class T>
static T& Field(void* base, uint32_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

struct SynthMesh {
  uint32_t* vtable;
  uint32_t kind;
  uint8_t pad0[0x38];
  uint32_t view_tag;
  uint32_t pass;
  uint8_t pad1[0x14];
  uint8_t flag;
  uint8_t pad2[0x23];
};
static_assert(sizeof(SynthMesh) == 0x80 && offsetof(SynthMesh, view_tag) == 0x40 && offsetof(SynthMesh, pass) == 0x44 && offsetof(SynthMesh, flag) == 0x5c);

// A faction mask: the index at +4 (the loop copies it into the tracker); its bytes pick colours of the faction's list.
struct SynthMask {
  uint8_t bytes[4];
  int32_t index;
  uint8_t more[248];
};

// Item flags of the colour loop.
constexpr uint32_t ITEM_CONST1 = 1;
constexpr uint32_t ITEM_CONST2 = 2;
constexpr uint32_t ITEM_TEXTURES = 4;
constexpr uint32_t ITEM_FACTION = 8;
// Sets the "any" byte and does not count: a chunk can end with the byte set and nothing to flush.
constexpr uint32_t ITEM_ANY_ONLY = 16;

struct SynthItem {
  SynthMesh* mesh;
  uint8_t pad0[0x54 - 4];
  const SynthMask* mask;
  uint32_t pad1;
  int32_t anim;
  uint32_t texture1;
  uint32_t texture2;
  int32_t faction;
  uint32_t value;
  uint32_t repeat;
  uint32_t flags;
  uint8_t pad2[0xec - 0x78];
  uint32_t variation;
  uint8_t pad3[0x100 - 0xf0];
};
static_assert(sizeof(SynthItem) == 0x100 && offsetof(SynthItem, mask) == 0x54 && offsetof(SynthItem, anim) == 0x5c
              && offsetof(SynthItem, variation) == 0xec);

struct SynthList {
  uint32_t pad;
  uint32_t count;
  SynthItem** data;
};
static_assert(offsetof(SynthList, count) == 4 && offsetof(SynthList, data) == 8);

// kind, mesh, the 10 flush arguments (or the call's arguments), view_tag, pass, the +0x69 byte the flush saw.
using LogEntry = std::array<uint32_t, 15>;
static std::vector<LogEntry> game_log;
// The depth worlds' fake flush reads the mesh state it should not need; the colour worlds' does what the game's flush does.
static bool strict_flush = true;
static dr::Shared* g_shared = nullptr;

// What the test changes in the thunk rule (the controls).
struct TestControls {
  bool drop_end_flush = false;
  uint32_t anim_stack = 9;
  int inject_chunk = -1;
  // The chunk (slot index) whose recording overflows at its first flush: the replay runs it again.
  int overflow_chunk = -1;
  // The flush entry's site id for every flush instead of the call-site ids.
  bool entry_site = false;
  // The mesh of the op an injection adds (0: the flush's own mesh).
  uint32_t inject_mesh = 0;
  // The chunk (slot index) whose first boundary flush op gets a zero header once recorded (a damaged op buffer).
  int corrupt_chunk = -1;
  // The corruption repeats in every round (not once).
  bool corrupt_keep = false;
};
static TestControls test_controls;

// A real flush that calls the dispatcher (A nesting): every nest_a_every-th flush, nest_a_budget times, runs nest_a_call.
static uint32_t nest_a_every = 0;
static uint32_t nest_a_flushes = 0;
static uint32_t nest_a_budget = 0;
static bool nest_a_running = false;
static void (*nest_a_call)() = nullptr;
// The mesh of the op the last injection added.
static uint32_t injected_mesh = 0;

// The real flush's tracker writes: the cursor +0x5c advances by count x stride, the +0x28 cache is written, the +0x69 byte is
// read and cleared. The pointers in the arguments are the tracker's.
static void __thiscall FakeFlush(void* mesh, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9) {
  // A flush of no instances returns at once (the colour loop can ask for one).
  if (a4 == 0) return;
  auto* m = static_cast<SynthMesh*>(mesh);
  auto* any = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(a3));
  if (strict_flush) {
    // The depth tests' flush reads the mesh's state (more than the game's does).
    game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, m->view_tag, m->pass, *any});
  } else {
    // The game's: +0x40 is written first (0x17783b9), +0x44 is never read (S1).
    m->view_tag = a1 ^ 0x77;
    game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, m->view_tag, 0, *any});
  }
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a6)) += a4 * (m->kind + 1);
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a8)) = a4 ^ a2;
  *any = 0;
  if (nest_a_every != 0 && nest_a_budget > 0 && !nest_a_running && ++nest_a_flushes % nest_a_every == 0) {
    --nest_a_budget;
    nest_a_running = true;
    nest_a_call();
    nest_a_running = false;
  }
}
static uint8_t __thiscall FakeVf68(void* mesh, uint32_t view, uint32_t zero) {
  const auto* m = static_cast<const SynthMesh*>(mesh);
  game_log.push_back({2, reinterpret_cast<uint32_t>(mesh), view, zero, 0, 0, 0, 0, 0, 0, 0, 0, m->view_tag, m->pass, 0});
  return static_cast<uint8_t>(m->kind & 1);
}
static void __thiscall FakeAnimCall(void* state, uint32_t value) {
  game_log.push_back({3, reinterpret_cast<uint32_t>(state), value, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}
// The virtual call at a batch's first instance (0x17b09ce): a stub in the census classes; one of a class outside the census may
// have effects (it logs here, at once, so a call made while recording lands before the replayed ops of the log).
static void __thiscall FakeForeignVf64(void* mesh) { game_log.push_back({7, reinterpret_cast<uint32_t>(mesh), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); }

static std::array<uint32_t, 0x70 / 4> mesh_vtable = {};
// A class the census did not cover.
static std::array<uint32_t, 0x70 / 4> foreign_vtable = {};

// The colour loop's environment: the node (a vtable with the faction lookup, the animation state, the second constant's block),
// four blocks of the first constant, the faction lists.
struct SynthColour {
  float c[3];
};
struct SynthFactionList {
  uint32_t pad;
  uint32_t count;
  SynthColour* data;
};
struct SynthBlock {
  uint8_t pad[0x14];
  float* cell;
  uint8_t more[0x28];
};
struct SynthNode {
  uint32_t* vtable;
  uint8_t pad[0x2950 - 4];
  uint8_t state[0x400];
};
static_assert(offsetof(SynthNode, state) == 0x2950 && 0x2950 + 0x2e8 + 4 <= sizeof(SynthNode));

static SynthNode synth_node;
static std::array<uint32_t, 0xc4 / 4> node_vtable = {};
static std::array<SynthBlock, 4> const1_blocks;
static std::array<float, 4> const1_cells;
static float const2_cell = 0;
static uint8_t cached_constants = 0;
static float faction_default = 0.25f;
static std::array<SynthColour, 256> synth_colours;
static std::array<SynthFactionList, 4> faction_lists;

static void __thiscall FakeDirty(uint32_t object) {
  uint32_t bits = 0;
  std::memcpy(&bits, *reinterpret_cast<float* const*>(static_cast<uintptr_t>(object) + 0x14), 4);
  game_log.push_back({4, object, bits});
}
static uint32_t __thiscall FakeView(uint32_t texture) { return texture == 0 ? 0 : texture ^ 0x1000; }
static void __thiscall FakeSrv(uint32_t slot, uint32_t view) { game_log.push_back({5, slot, view}); }
static void __thiscall FakeFactionColours(uint32_t state, const float* colours) {
  LogEntry entry = {6, state};
  std::memcpy(&entry[2], colours, 48);
  game_log.push_back(entry);
}
static void __thiscall FakeFactionLists(void*, int32_t faction, uint32_t* list) { *list = reinterpret_cast<uint32_t>(&faction_lists[static_cast<uint32_t>(faction) & 3]); }
// A node whose list call leaves its out pointer alone (N-7: the bytes do not prove that every node writes it).
static void __thiscall FakeFactionListsSilent(void*, int32_t, uint32_t*) {}
static std::array<uint32_t, 0xc4 / 4> silent_vtable = {};
static const uint32_t* silent_node = nullptr;

// What the game does at those sites, written from its listing (the replay's own functions are checked against these).
static void GameConst(uint32_t object, uint32_t bits) {
  float value = 0;
  std::memcpy(&value, &bits, 4);
  auto* const cell = *reinterpret_cast<float**>(static_cast<uintptr_t>(object) + 0x14);
  if (cached_constants == 0 || !(*cell == value)) {
    std::memcpy(cell, &bits, 4);
    FakeDirty(object);
  }
}
static void GameSrv(uint32_t slot, uint32_t texture) { FakeSrv(slot, FakeView(texture)); }
static void GameFaction(int32_t faction, const SynthMask* mask, void* node, uint32_t state) {
  if (faction == -1) {
    const std::array<float, 12> entries = {faction_default, faction_default, 1.0f, faction_default, faction_default, 1.0f,
                                           faction_default, faction_default, 1.0f, faction_default, faction_default, 1.0f};
    FakeFactionColours(state, entries.data());
    return;
  }
  uint32_t found = 0;
  const auto lookup = reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t*)>((*static_cast<uint32_t**>(node))[0xc0 / 4]);
  lookup(node, faction, &found);
  const auto* list = reinterpret_cast<const SynthFactionList*>(found);
  std::vector<SynthColour> gathered;
  const SynthColour* chosen = list->data;
  uint32_t count = list->count;
  if (mask != nullptr) {
    for (uint32_t j = 0; j < list->count; ++j) gathered.push_back(list->data[reinterpret_cast<const uint8_t*>(mask)[j]]);
    chosen = gathered.data();
    count = static_cast<uint32_t>(gathered.size());
  }
  if (count >= 4) FakeFactionColours(state, chosen[0].c);
}

static bool InArena(void* tracker) { return reinterpret_cast<uint32_t>(tracker) - g_shared->arena < g_shared->arena_size; }

struct StackImage {
  dr::Frame frame;
  uint32_t stack[16];
};

static StackImage MakeImage(dr::Site site, uint32_t ecx) {
  StackImage image = {};
  image.frame.site = static_cast<uint32_t>(site);
  image.frame.ecx = ecx;
  image.frame.eax = 0x55555555;
  for (uint32_t i = 0; i < 16; ++i) image.stack[i] = 0xdead0000 + i;
  return image;
}

static void FlushSite(dr::Site site, void* t, uint32_t mesh, uint32_t technique, uint32_t count, uint32_t last_argument = 0) {
  const uint32_t address = reinterpret_cast<uint32_t>(t);
  const std::array<uint32_t, 10> args = {Field<uint32_t>(t, 0x18), Field<uint32_t>(t, 0x1c), technique, address + 0x69, count,
                                         Field<uint32_t>(t, 0x24), address + 0x5c, Field<uint32_t>(t, 0x78), address + 0x28,
                                         last_argument};
  if (!InArena(t)) {
    FakeFlush(reinterpret_cast<void*>(mesh), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    return;
  }
  auto* slot = static_cast<dr::Slot*>(t);
  if (test_controls.drop_end_flush && (site == dr::Site::DEPTH_FLUSH_END || site == dr::Site::COLOUR_FLUSH_END) && slot->mode == dr::MODE_RECORD) return;
  StackImage image = MakeImage(test_controls.entry_site ? dr::Site::FLUSH_ENTRY : site, mesh);
  for (uint32_t i = 0; i < 10; ++i) image.stack[1 + i] = args[i];
  if (test_controls.overflow_chunk == static_cast<int>(slot->chunk) && slot->mode == dr::MODE_RECORD) {
    slot->flags |= dr::SLOT_OVERFLOW;
  }
  dr::Record(slot, &image.frame);
  if (test_controls.inject_chunk == static_cast<int>(slot->chunk) && slot->mode == dr::MODE_RECORD
      && (site == dr::Site::DEPTH_FLUSH_BOUNDARY || site == dr::Site::COLOUR_FLUSH_BOUNDARY)) {
    test_controls.inject_chunk = -1;
    injected_mesh = (test_controls.inject_mesh != 0 ? test_controls.inject_mesh : mesh);
    image.frame.ecx = injected_mesh;
    dr::Record(slot, &image.frame);
  }
  if (test_controls.corrupt_chunk == static_cast<int>(slot->chunk) && slot->mode == dr::MODE_RECORD
      && (site == dr::Site::DEPTH_FLUSH_BOUNDARY || site == dr::Site::COLOUR_FLUSH_BOUNDARY)
      && slot->ops_cursor >= slot->ops_begin + 1 + di::FLUSH_PAYLOAD) {
    if (!test_controls.corrupt_keep) test_controls.corrupt_chunk = -1;
    *(slot->ops_cursor - (1 + di::FLUSH_PAYLOAD)) = 0;
  }
}

static uint8_t Vf68Site(dr::Site site, void* t, uint32_t mesh, uint32_t view) {
  if (!InArena(t)) {
    const auto function = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(static_cast<uintptr_t>((*reinterpret_cast<uint32_t**>(mesh))[0x68 / 4]));
    return function(reinterpret_cast<void*>(mesh), view, 0);
  }
  StackImage image = MakeImage(site, mesh);
  image.stack[1] = view;
  image.stack[2] = 0;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return static_cast<uint8_t>(image.frame.eax);
}

static void AnimSite(dr::Site site, void* t, uint32_t state, uint32_t value, uint32_t index) {
  if (!InArena(t)) {
    g_shared->last_anim = static_cast<int32_t>(index);
    FakeAnimCall(reinterpret_cast<void*>(state), value);
    return;
  }
  StackImage image = MakeImage(site, state);
  image.stack[1] = value;
  // The colour loop's frame is two dwords deeper.
  image.stack[test_controls.anim_stack + (site == dr::Site::COLOUR_ANIM ? 2 : 0)] = index;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

// The colour loop's other sites: outside the arena the game's own code, inside a Record frame.
static bool SetupSite(void* t, uint32_t mesh) {
  if (!InArena(t)) return false;
  StackImage image = MakeImage(dr::Site::COLOUR_SETUP, 0);
  image.frame.ebx = mesh;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return image.frame.eax != 0;
}

static void Const1Site(void* t, uint32_t block) {
  if (!InArena(t)) {
    GameConst(block, 0);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_CONST1, block);
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void Const2Site(void* t, uint32_t state, uint32_t bits) {
  if (!InArena(t)) {
    GameConst(state + di::CONST2_BLOCK, bits);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_CONST2, 0);
  image.frame.xmm0 = bits;
  image.stack[5] = state;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void SrvSite(dr::Site site, void* t, uint32_t mesh, uint32_t texture) {
  if (!InArena(t)) {
    GameSrv(mesh + (site == dr::Site::COLOUR_SRV_DIFFUSE ? 0x41f4 : 0x421c), texture);
    return;
  }
  StackImage image = MakeImage(site, texture);
  image.frame.ebx = mesh;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

static void FactionSite(void* t, SynthItem* item, SynthNode* node, uint32_t state) {
  if (!InArena(t)) {
    Field<int32_t>(t, 0x0c) = (item->mask != nullptr ? item->mask->index : -1);
    GameFaction(item->faction, item->mask, node, state);
    return;
  }
  StackImage image = MakeImage(dr::Site::COLOUR_FACTION, static_cast<uint32_t>(item->faction));
  image.frame.edi = reinterpret_cast<uint32_t>(item);
  image.stack[0x28 / 4] = reinterpret_cast<uint32_t>(node);
  image.stack[0x14 / 4] = state;
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
}

// The depth loop, from its listing: the thunk rule on its init, flush, vf68 and animation sites.
static void __thiscall SynthDepthLoop(void* node, void* t, void* list_pointer, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  if (list->count == 0) return;
  int32_t anim = (InArena(t) ? static_cast<dr::Slot*>(t)->anim : g_shared->seed_anim);
  const uint32_t state = reinterpret_cast<uint32_t>(node) + 0x2950;
  for (uint32_t i = begin; i < end; ++i) {
    SynthItem* item = list->data[i];
    SynthMesh* mesh = item->mesh;
    const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::DEPTH_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t type = mesh->kind ^ (mesh->view_tag & 0x10000);
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t kind = type & 0xffff;
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          // The stride the driver reads; written only by a mesh with an enabled technique.
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::DEPTH_VF68, t, mesh_address, Field<uint32_t>(t, 0x1c));
          // A path that sets the "any" byte itself.
          if (kind == 3) Field<uint8_t>(t, 0x69) = 1;
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    const uint32_t technique = Field<uint32_t>(t, 0x74);
    if (technique == 0) continue;
    if (anim != item->anim) {
      const uint32_t pending = Field<uint32_t>(t, 0x58);
      if (pending != 0) {
        FlushSite(dr::Site::DEPTH_FLUSH_ANIM, t, Field<uint32_t>(t, 0x6c), technique, pending);
        Field<uint32_t>(t, 0x58) = 0;
      }
      Field<uint32_t>(t, 0x78) = 8;
      anim = item->anim;
      AnimSite(dr::Site::DEPTH_ANIM, t, state, static_cast<uint32_t>(anim) * 3 + 1, static_cast<uint32_t>(anim));
    }
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::DEPTH_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending);
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

// The colour loop, from its listing (0x17b0400): the depth loop's mesh change, plus the vtable check at the setup, the variation
// path (+0x14 = the item's variation, written into the mesh's +0x44 with the shared-state call after it), the constants, the
// textures and faction block (each only when the tracker's fields +0x00..+0x0c say the item changed them), and the item's
// end: the pure virtual call that sets the "any" byte on the first instance. Mesh kinds from 6 on have no shared-state call at
// their setup (lets the constants of two chunks touch).
// Where it is NOT the listing's loop (C.3's test 2 runs the real bytes; the recording does not depend on these being absent):
//  - no flag machine at +0x10..+0x13 (0x17b06db sets +0x11 and drives the first constant, 0x17b0783 clears +0x10, 0x17b0708
//    clears +0x12 / +0x13): the seed bytes 0x10..0x13 never differ here, so the "flags" miss counter stays 0 in these tests;
//  - the "any" byte's source at a change (0x17b0745) is not modelled: the sources here are the batch start and ITEM_ANY_ONLY
//    items, which the game does not have (they reach the pending-any compare and the constant merge across chunk ends);
//  - the game flushes at every change of the mask (before a texture, faction or animation-only change); this loop flushes only
//    before an animation change;
//  - texture 0 is "no change" in the game (never written to +0x04 / +0x08); here 0 is written and bound;
//  - the variation path's vf58 / vf60 / +0x74 rewrite (0x17b0618) and the batch-size flush (vf6c, 0x17b05b6) are missing: the
//    variation path here is flush, write the mesh, shared-state call;
//  - kind-6 meshes (no shared-state call at the setup) and ANY_ONLY items are not paths of the game;
//  - the batchfix continuation (+0x14 kept with an equal variation) is not modelled (C.3's test 4);
//  - the virtual call at a batch's first instance (0x17b09ce) is a stub for the census classes; here a class outside the census
//    logs when it is called (FakeForeignVf64), which the continuation tests use.
static void __thiscall SynthColourLoop(void* node_pointer, void* t, void* list_pointer, uint32_t, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  if (list->count == 0) return;
  auto* node = static_cast<SynthNode*>(node_pointer);
  int32_t anim = (InArena(t) ? static_cast<dr::Slot*>(t)->anim : g_shared->seed_anim);
  const uint32_t state = reinterpret_cast<uint32_t>(node->state);
  for (uint32_t i = begin; i < end; ++i) {
    SynthItem* item = list->data[i];
    SynthMesh* mesh = item->mesh;
    const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::COLOUR_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      if (SetupSite(t, mesh_address)) {
        Field<uint32_t>(t, 0x74) = 0;
        continue;
      }
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t type = mesh->kind ^ (mesh->view_tag & 0x10000);
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t kind = type & 0xffff;
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          if (mesh->kind < 6) Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_VF68_SETUP, t, mesh_address, Field<uint32_t>(t, 0x1c));
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    const uint32_t technique = Field<uint32_t>(t, 0x74);
    if (technique == 0) continue;
    // The variation path: a full batch or another variation flushes; a new variation is written into the mesh.
    const uint32_t pending = Field<uint32_t>(t, 0x58);
    if (pending == Field<uint32_t>(t, 0x64) || item->variation != Field<uint32_t>(t, 0x14)) {
      FlushSite(dr::Site::COLOUR_FLUSH_VARIATION, t, Field<uint32_t>(t, 0x70), technique, pending);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
      if (item->variation != Field<uint32_t>(t, 0x14)) {
        Field<uint32_t>(t, 0x14) = item->variation;
        mesh->pass = item->variation;
        Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_VF68_VARIATION, t, mesh_address, Field<uint32_t>(t, 0x1c));
      }
    }
    // (An item that only sets the "any" byte leaves the animation alone, so a chunk can end with the change mask clear.)
    if (anim != item->anim && (item->flags & ITEM_ANY_ONLY) == 0) {
      const uint32_t waiting = Field<uint32_t>(t, 0x58);
      if (waiting != 0) {
        FlushSite(dr::Site::COLOUR_FLUSH_CHANGE, t, Field<uint32_t>(t, 0x6c), technique, waiting);
        Field<uint32_t>(t, 0x58) = 0;
      }
      Field<uint32_t>(t, 0x78) = 8;
      anim = item->anim;
      AnimSite(dr::Site::COLOUR_ANIM, t, state, static_cast<uint32_t>(anim) * 3 + 1, static_cast<uint32_t>(anim));
    }
    if ((item->flags & ITEM_CONST1) != 0) Const1Site(t, reinterpret_cast<uint32_t>(&const1_blocks[mesh->kind & 3]));
    if ((item->flags & ITEM_CONST2) != 0) {
      for (uint32_t r = 0; r < item->repeat; ++r) Const2Site(t, state, item->value);
    }
    if ((item->flags & ITEM_TEXTURES) != 0 && (item->texture1 != Field<uint32_t>(t, 0x04) || item->texture2 != Field<uint32_t>(t, 0x08))) {
      Field<uint32_t>(t, 0x04) = item->texture1;
      SrvSite(dr::Site::COLOUR_SRV_DIFFUSE, t, mesh_address, item->texture1);
      Field<uint32_t>(t, 0x08) = item->texture2;
      SrvSite(dr::Site::COLOUR_SRV_NORMAL, t, mesh_address, item->texture2);
    }
    if ((item->flags & ITEM_FACTION) != 0) {
      const int32_t mask_index = (item->mask != nullptr ? item->mask->index : -1);
      if (item->faction != Field<int32_t>(t, 0x00) || mask_index != Field<int32_t>(t, 0x0c)) {
        Field<int32_t>(t, 0x00) = item->faction;
        FactionSite(t, item, node, state);
      }
    }
    if (Field<uint32_t>(t, 0x58) == 0 && Field<uint8_t>(t, 0x68) != 0) {
      Field<uint8_t>(t, 0x69) |= (mesh->kind >> 1) & 1;
      if (mesh->vtable[0x64 / 4] != 0) reinterpret_cast<void(__thiscall*)(void*)>(mesh->vtable[0x64 / 4])(mesh);
    }
    if ((item->flags & ITEM_ANY_ONLY) != 0) {
      Field<uint8_t>(t, 0x69) |= 1;
      continue;
    }
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::COLOUR_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending);
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

struct Spec {
  uint32_t items = 6000;
  uint32_t seed = 1;
  uint32_t max_run = 8;
  uint32_t none_percent = 10;
  uint32_t anim_minus_percent = 0;
  uint32_t anim_change_percent = 20;
  uint32_t max_count = 1000;
  bool pattern = false;
  // Items from this percent of the list on use meshes without an enabled technique.
  uint32_t enabled_percent = 100;
  // The list starts with 40 items of one mesh, and the tracker starts as a continuation of it with foreign mesh state.
  bool continuation = false;
  // The driver's portion = this / the tracker's stride.
  uint32_t portion_numerator = 12000;
  // The colour loop's list: items with the colour fields.
  bool colour = false;
  // Every run of items gets a mesh of its own (the list never returns to a mesh).
  bool unique_meshes = false;
  // The first and the last items but 5 share a mesh (with unique_meshes).
  bool repeat_far = false;
  // Meshes of a class outside the census.
  uint32_t foreign_percent = 0;
  uint32_t variation_percent = 20;
  // Items that only set the "any" byte (no count).
  uint32_t any_only_percent = 0;
  // Meshes of kind 6 (no shared-state call at the setup), constants of one value, mostly any-only items.
  bool merge = false;
  // Items that set the first constant / the second constant / the textures / the faction block.
  uint32_t const1_percent = 50;
  uint32_t const2_percent = 60;
  uint32_t texture_percent = 60;
  uint32_t faction_percent = 50;
};

struct World {
  std::vector<SynthMesh> meshes;
  std::vector<SynthItem> items;
  std::vector<SynthItem*> pointers;
  std::array<SynthMask, 3> masks = {};
  bool colour = false;
  SynthList list = {};
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  uint32_t max_count = 1000;
  uint32_t portion_numerator = 12000;
  bool continuation = false;
  // The tracker's state when a call starts (a call the loop itself ended leaves both 0).
  uint32_t pending = 0;
  uint32_t change_mask = 0;
  // The mesh the tracker starts on, in the state a call's own setup would leave it in (the guard accepts it): a continuation
  // that is recorded, not run directly.
  SynthMesh* start_mesh = nullptr;
  // The colour world's flush reads the mesh state like the depth worlds' does.
  bool strict = false;
};

// The tests' random numbers (xorshift): the same list for the same seed.
struct Rng {
  uint64_t state;
  uint32_t operator()(uint32_t bound) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<uint32_t>((state >> 11) % bound);
  }
};

// The colour fields of a list's items (and the faction tables they index): the same random walk for every world.
static void FillColourFields(std::vector<SynthItem>* items, std::array<SynthMask, 3>* masks, const Spec& spec, Rng* random_state) {
  Rng& random = *random_state;
  // Factions 0 to 3 have lists of 6, 3, 70 and 4 colours (all pointing into one table); -1 is the default.
  for (uint32_t e = 0; e < synth_colours.size(); ++e) {
    synth_colours[e] = {{1.0f + 0.5f * static_cast<float>(e), 0.25f * static_cast<float>(e), 0.5f + 0.125f * static_cast<float>(e)}};
  }
  const std::array<uint32_t, 4> list_counts = {6, 3, 70, 4};
  for (uint32_t f = 0; f < 4; ++f) faction_lists[f] = {.count = list_counts[f], .data = synth_colours.data()};
  for (uint32_t m = 0; m < (*masks).size(); ++m) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&(*masks)[m]);
    for (uint32_t j = 0; j < sizeof(SynthMask); ++j) bytes[j] = static_cast<uint8_t>(j * 37 + m * 11);
    (*masks)[m].index = static_cast<int32_t>(10 + m);
    for (uint32_t j = 0; j < 4; ++j) (*masks)[m].bytes[j] = static_cast<uint8_t>(j + 3 * m);
  }
  const std::array<uint32_t, 6> values = {0x00000000, 0x80000000, 0x7fc00000, 0x3f800000, 0x40000000, 0x3f800000};
  int32_t faction = -1;
  uint32_t value_index = 0;
  uint32_t texture1 = 1;
  uint32_t texture2 = 1;
  for (SynthItem& item : (*items)) {
    // Fields change now and then, so the tracker's detection fields hold across items and across a chunk's first item.
    if (random(100) < 15) faction = static_cast<int32_t>(random(5)) - 1;
    if (random(100) < 20) value_index = random(static_cast<uint32_t>(values.size()));
    if (random(100) < 25) texture1 = random(4);
    if (random(100) < 25) texture2 = random(3);
    item.faction = faction;
    item.value = values[value_index];
    item.texture1 = texture1;
    item.texture2 = texture2;
    item.repeat = 1 + random(3);
    item.variation = (random(100) < spec.variation_percent ? 1 + random(3) : 0);
    item.mask = (random(3) == 0 ? nullptr : &(*masks)[random(3)]);
    item.flags = (random(100) < spec.const1_percent ? ITEM_CONST1 : 0) | (random(100) < spec.const2_percent ? ITEM_CONST2 : 0)
                 | (random(100) < spec.texture_percent ? ITEM_TEXTURES : 0) | (random(100) < spec.faction_percent ? ITEM_FACTION : 0)
                 | (random(100) < spec.any_only_percent ? ITEM_ANY_ONLY : 0);
    if (spec.merge) {
      // One animation for the whole list (the loop starts with it): every seed hits.
      item.flags = ITEM_CONST2 | (random(100) < 70 ? ITEM_ANY_ONLY : 0);
      item.value = values[3];
      item.variation = 0;
      item.anim = -1;
    }
  }
}

static std::unique_ptr<World> BuildWorld(const Spec& spec) {
  auto world = std::make_unique<World>();
  Rng random{spec.seed * 0x9e3779b97f4a7c15ull + 1};
  world->max_count = spec.max_count;
  world->portion_numerator = spec.portion_numerator;
  world->continuation = spec.continuation;
  world->colour = spec.colour;
  world->meshes.resize(spec.unique_meshes ? spec.items + 1 : 60);
  for (uint32_t m = 0; m < world->meshes.size(); ++m) {
    SynthMesh& mesh = world->meshes[m];
    mesh.vtable = ((spec.foreign_percent != 0 && random(100) < spec.foreign_percent) ? foreign_vtable.data() : mesh_vtable.data());
    mesh.flag = static_cast<uint8_t>(m & 1);
    if (spec.merge) {
      mesh.kind = 6;
    } else if (spec.pattern) {
      mesh.kind = (m < 6 ? 2 + m % 4 : 0);
    } else if (spec.enabled_percent < 100) {
      mesh.kind = (m < 4 ? m % 2 : 2 + random(4));
    } else if (random(100) < spec.none_percent) {
      mesh.kind = random(2);
    } else {
      mesh.kind = 2 + random(4);
    }
  }
  world->items.resize(spec.items);
  uint32_t i = 0;
  int32_t anim = 0;
  uint32_t pattern_run = 0;
  while (i < spec.items) {
    uint32_t mesh_index = 0;
    uint32_t run = 1;
    if (spec.unique_meshes) {
      mesh_index = pattern_run++;
      run = 1 + random(1u << random(1 + static_cast<uint32_t>(std::bit_width(spec.max_run))));
      run = std::min(run, spec.max_run);
    } else if (spec.pattern) {
      // Five items of a valid mesh, three of one without a technique.
      const bool none = (pattern_run % 2 == 1);
      mesh_index = (none ? 6 + (pattern_run / 2) % 4 : (pattern_run / 2) % 6);
      run = (none ? 3 : 5);
      ++pattern_run;
    } else {
      if (spec.enabled_percent < 100) {
        const bool tail = (static_cast<uint64_t>(i) * 100 >= static_cast<uint64_t>(spec.items) * spec.enabled_percent);
        mesh_index = (tail ? random(4) : 4 + random(static_cast<uint32_t>(world->meshes.size()) - 4));
      } else {
        mesh_index = random(static_cast<uint32_t>(world->meshes.size()));
      }
      // Log-uniform lengths from 1 to max_run.
      run = 1 + random(1u << random(1 + static_cast<uint32_t>(std::bit_width(spec.max_run))));
      run = std::min(run, spec.max_run);
    }
    if (!spec.pattern && random(100) < spec.anim_minus_percent) anim = -1;
    for (uint32_t r = 0; r < run && i < spec.items; ++r, ++i) {
      if (spec.pattern) {
        anim = 5;
      } else if (random(100) < spec.anim_change_percent) {
        anim = (random(100) < spec.anim_minus_percent ? -1 : static_cast<int32_t>(random(7)));
      }
      world->items[i].mesh = &world->meshes[mesh_index];
      world->items[i].anim = anim;
    }
  }
  if (spec.continuation) {
    world->meshes[0].kind = 2;
    for (uint32_t r = 0; r < 40 && r < spec.items; ++r) {
      world->items[r].mesh = &world->meshes[0];
      world->items[r].anim = 0;
    }
    // The colour list returns to the mesh once, a little later in the same chunk.
    for (uint32_t r = 100; spec.colour && r < 110 && r < spec.items; ++r) world->items[r].mesh = &world->meshes[0];
  }
  if (spec.repeat_far) {
    for (uint32_t r = 0; r < 5; ++r) world->items[spec.items - 5 + r].mesh = world->items[5].mesh;
  }
  if (spec.unique_meshes && spec.enabled_percent < 100) {
    // The meshes of the last items have no technique.
    for (uint32_t r = spec.items * spec.enabled_percent / 100; r < spec.items; ++r) world->items[r].mesh->kind = random(2);
  }
  if (spec.colour) {
    FillColourFields(&world->items, &world->masks, spec, &random);
  }
  world->pointers.resize(spec.items);
  for (uint32_t k = 0; k < spec.items; ++k) world->pointers[k] = &world->items[k];
  world->list = {.count = spec.items, .data = world->pointers.data()};
  return world;
}

// A colour list (Spec::unique_meshes) whose first `run` items are one mesh (the world's last, which no run uses) that the
// tracker starts on in a state the guard accepts: the first call is a continuation that is recorded.
static std::unique_ptr<World> BuildContinuedWorld(const Spec& spec, uint32_t run) {
  auto world = BuildWorld(spec);
  SynthMesh* mesh = &world->meshes.back();
  mesh->kind = 2;
  mesh->flag = 1;
  for (uint32_t i = 0; i < run; ++i) world->items[i].mesh = mesh;
  world->start_mesh = mesh;
  return world;
}

struct Outcome {
  std::vector<LogEntry> log;
  std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  std::vector<std::array<uint32_t, 2>> mesh_fields;
  // The constants' cells at the end (bits).
  std::array<uint32_t, 5> cells = {};
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  uint32_t direct_calls = 0;
  // The driver's calls, and those with fewer items than the dispatcher's floor.
  uint32_t portions = 0;
  uint32_t small_calls = 0;
  std::array<uint32_t, 6> compares = {};
  // Trace frames: the calls that ran directly because a mesh recurred in two chunks, the ops by kind, and the constants that a
  // chunk end split (the compare run's op went on in the next chunk).
  uint32_t conflicts = 0;
  std::array<uint32_t, static_cast<size_t>(di::OpKind::COUNT)> kind_ops = {};
  uint32_t split_constants = 0;
  // Calls whose first item continued the tracker's mesh (no setup before it) and that were replayed, not run directly.
  uint32_t continued = 0;
  // Trace frames: the seed misses, by field (faction, decals, mask, flags, animation), and the replayed constants' repeats.
  uint32_t seed_misses = 0;
  std::array<uint32_t, 5> miss_fields = {};
  uint32_t constant_repeats = 0;
  // Trace frames: every miss is counted under at least one field.
  bool misses_explained = true;
  // Trace frames: total + the trace-only phases == wall, in every call.
  bool accounted = true;
  uint32_t most_buffer = 0;
  std::string first_reason;
  // B rounds: direct segments, chunks the entry check ran again, chunks with no seed, meshes twice in a round, the items the
  // planner gave the pool and the ones it kept.
  uint32_t direct_segments = 0;
  uint32_t entry_reruns = 0;
  uint32_t unknown_seeds = 0;
  uint32_t duplicates = 0;
  uint32_t census_items = 0;
  uint32_t direct_items = 0;
};

static bool CompareDiffers(const Outcome& outcome) {
  return outcome.compares[static_cast<size_t>(di::CompareResult::DIFFERS)] + outcome.compares[static_cast<size_t>(di::CompareResult::COMPARE_HAS_MORE)] > 0;
}

// A world's own state at the start of a run (the shared page and the log are ResetWorld's).
static void ResetState(World* world) {
  for (SynthMesh& mesh : world->meshes) {
    mesh.view_tag = 0xffffffff;
    mesh.pass = 0xffffffff;
  }
  world->tracker = {};
  uint8_t* t = world->tracker.data();
  Field<uint32_t>(t, 0x18) = 0xd0;
  Field<uint32_t>(t, 0x1c) = 0xe0;
  Field<uint32_t>(t, 0x20) = 0x12345;
  Field<uint32_t>(t, 0x24) = 0xb0;
  Field<uint32_t>(t, 0x50) = 6;
  Field<uint32_t>(t, 0x64) = world->max_count;
  strict_flush = (!world->colour || world->strict);
  if (world->colour) {
    // The detection fields start unlike any item's; the constants' cells and the cache flag are the test's.
    Field<int32_t>(t, 0x00) = -2;
    Field<int32_t>(t, 0x0c) = -2;
    // The flush writes the mesh's +0x40 with the value the setup wrote (the view's type, once per tracker).
    Field<uint32_t>(t, 0x20) = Field<uint32_t>(t, 0x1c) ^ 0x77;
    const1_cells.fill(1.0f);
    const2_cell = 0.5f;
  }
  if (world->continuation || world->pending != 0 || world->change_mask != 0) {
    // A call that starts in the middle of a mesh whose state is another view's (or not this call's).
    SynthMesh* mesh = (world->continuation ? world->items[0].mesh : &world->meshes.back());
    Field<uint32_t>(t, 0x70) = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x74) = 0x20;
    Field<uint32_t>(t, 0x58) = world->pending;
    Field<uint32_t>(t, 0x78) = world->change_mask;
    mesh->view_tag = 0xabc;
    mesh->pass = 0x77;
  }
  if (world->start_mesh != nullptr) {
    Field<uint32_t>(t, 0x70) = reinterpret_cast<uint32_t>(world->start_mesh);
    Field<uint32_t>(t, 0x74) = 0x20;
    Field<uint8_t>(t, 0x68) = world->start_mesh->flag;
    world->start_mesh->view_tag = Field<uint32_t>(t, 0x20);
    world->start_mesh->pass = 0;
  }
}

static void ResetWorld(World* world) {
  ResetState(world);
  g_shared->seed_anim = -1;
  g_shared->last_anim = -1;
  game_log.clear();
}

static Outcome Capture(World* world) {
  Outcome outcome = {.log = game_log, .tracker = world->tracker};
  for (const SynthMesh& mesh : world->meshes) outcome.mesh_fields.push_back({mesh.view_tag, mesh.pass});
  for (uint32_t i = 0; i < 4; ++i) std::memcpy(&outcome.cells[i], &const1_cells[i], 4);
  std::memcpy(&outcome.cells[4], &const2_cell, 4);
  return outcome;
}

static void* const NODE = reinterpret_cast<void*>(0x1000);
// The world the nesting flush's call runs (DepthNested).
static World* nest_a_world_pointer = nullptr;

// The serial loop of the world's group, and the same call through its dispatcher.
static void SerialPortion(World* world, uint32_t begin, uint32_t end) {
  if (world->colour) {
    SynthColourLoop(&synth_node, world->tracker.data(), &world->list, 0, begin, end);
  } else {
    SynthDepthLoop(NODE, world->tracker.data(), &world->list, begin, end);
  }
}

static void DispatchPortion(World* world, uint32_t begin, uint32_t end) {
  if (world->colour) {
    dr::DispatchColour(&synth_node, nullptr, world->tracker.data(), &world->list, 0, begin, end);
  } else {
    dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, begin, end);
  }
}

// The driver: portions of the list sized from the tracker's +0x50 (read after each portion), back to back on one tracker.
template <class RunPortion>
static void Driver(World* world, RunPortion&& run_portion) {
  const uint32_t total = world->list.count;
  for (uint32_t begin = 0; begin < total;) {
    const uint32_t stride = std::max<uint32_t>(1, Field<uint32_t>(world->tracker.data(), 0x50));
    const uint32_t end = std::min(total, begin + world->portion_numerator / stride);
    run_portion(begin, end);
    begin = end;
  }
}

static Outcome RunSerial(World* world) {
  ResetWorld(world);
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) { SerialPortion(world, begin, end); });
  }
  return Capture(world);
}

// Two passes of the driver through the dispatcher (the world is reset and the settings applied).
static Outcome RunCalls(World* world, bool tracing) {
  Outcome total = {};
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) {
      const bool continues = (reinterpret_cast<uint32_t>(world->items[begin].mesh) == Field<uint32_t>(world->tracker.data(), 0x70)
                              && Field<uint32_t>(world->tracker.data(), 0x14) == 0);
      DispatchPortion(world, begin, end);
      ++total.portions;
      total.small_calls += (end - begin < di::MIN_ITEMS ? 1 : 0);
      total.chunks += di::last_call.chunks;
      total.reruns += di::last_call.reruns;
      total.replayed += di::last_call.replayed;
      total.ops += di::last_call.ops;
      total.direct_calls += (di::last_call.direct ? 1 : 0);
      total.split_constants += di::last_call.split_constants;
      total.continued += (continues && di::last_call.replayed > 0 ? 1 : 0);
      if (tracing && !di::trace.calls.empty()) {
        const di::CallTrace& call = di::trace.calls.back();
        ++total.compares[static_cast<size_t>(call.compare_result)];
        if (total.first_reason.empty()) total.first_reason = call.direct_reason;
        total.conflicts += (std::string_view(call.direct_reason) == "conflict" ? 1 : 0);
        for (size_t kind = 0; kind < call.ops.size(); ++kind) total.kind_ops[kind] += call.ops[kind];
        total.seed_misses += call.seed_misses;
        uint32_t explained = 0;
        for (size_t field = 0; field < call.seed_miss_fields.size(); ++field) {
          total.miss_fields[field] += call.seed_miss_fields[field];
          explained += call.seed_miss_fields[field];
        }
        total.misses_explained = total.misses_explained && explained >= call.seed_misses;
        total.constant_repeats += call.constant_repeats;
      }
    });
  }
  if (tracing) {
    for (const di::CallTrace& call : di::trace.calls) {
      const uint64_t trace_only = call.compare + call.run_scan + (call.stage == dr::Stage::CHUNKED ? call.warm_ups : 0);
      total.accounted = total.accounted && call.total + trace_only == call.wall;
      total.most_buffer = std::max(total.most_buffer, call.op_buffer_bytes);
    }
  }
  di::trace.active = false;
  Outcome outcome = Capture(world);
  outcome.chunks = total.chunks;
  outcome.reruns = total.reruns;
  outcome.replayed = total.replayed;
  outcome.ops = total.ops;
  outcome.direct_calls = total.direct_calls;
  outcome.portions = total.portions;
  outcome.small_calls = total.small_calls;
  outcome.compares = total.compares;
  outcome.conflicts = total.conflicts;
  outcome.kind_ops = total.kind_ops;
  outcome.split_constants = total.split_constants;
  outcome.continued = total.continued;
  outcome.seed_misses = total.seed_misses;
  outcome.miss_fields = total.miss_fields;
  outcome.constant_repeats = total.constant_repeats;
  outcome.misses_explained = total.misses_explained;
  outcome.accounted = total.accounted;
  outcome.most_buffer = total.most_buffer;
  outcome.first_reason = total.first_reason;
  return outcome;
}

static Outcome RunStaged(World* world, dr::Stage stage, uint32_t threads, bool tracing = false, bool update = true) {
  ResetWorld(world);
  if (update) UpdateAt(threads, {.stage = stage, .loops = (world->colour ? 2u : 1u)});
  di::trace.active = tracing;
  di::trace.frames_left = 1u << 30;
  di::trace.calls.clear();
  return RunCalls(world, tracing);
}

static bool Equal(const Outcome& serial, const Outcome& staged, const char* label, bool print = true) {
  bool equal = true;
  if (serial.log != staged.log) {
    equal = false;
    if (print) {
      size_t at = 0;
      while (at < std::min(serial.log.size(), staged.log.size()) && serial.log[at] == staged.log[at]) ++at;
      std::printf("  %s: logs differ at entry %zu of %zu / %zu\n", label, at, serial.log.size(), staged.log.size());
      for (const std::vector<LogEntry>* log : {&serial.log, &staged.log}) {
        if (at >= log->size()) continue;
        std::printf("    %s:", (log == &serial.log ? "serial" : "staged"));
        for (const uint32_t word : (*log)[at]) std::printf(" %x", word);
        std::printf("\n");
      }
    }
  }
  if (serial.tracker != staged.tracker) {
    equal = false;
    if (print) std::printf("  %s: tracker bytes differ\n", label);
  }
  if (serial.mesh_fields != staged.mesh_fields) {
    equal = false;
    if (print) std::printf("  %s: mesh fields differ\n", label);
  }
  if (serial.cells != staged.cells) {
    equal = false;
    if (print) std::printf("  %s: constant cells differ\n", label);
  }
  return equal;
}

static const std::array<dr::Stage, 4> RECORD_STAGES = {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS};
static const char* StageName(dr::Stage stage) {
  switch (stage) {
    case dr::Stage::CHUNKED: return "chunked";
    case dr::Stage::RECORDED: return "recorded";
    case dr::Stage::ONE_WORKER: return "one worker";
    case dr::Stage::WORKERS: return "workers";
    case dr::Stage::TOUCH_AHEAD: return "touch-ahead";
    default: return "?";
  }
}

struct ListCase {
  const char* name;
  Spec spec;
  bool expect_chunks;
};

static std::vector<ListCase> Lists() {
  std::vector<ListCase> lists;
  lists.push_back({"short runs", {.items = 6000, .seed = 1, .max_run = 8}, true});
  lists.push_back({"runs 1 to 300", {.items = 9000, .seed = 2, .max_run = 300, .none_percent = 15}, true});
  lists.push_back({"animation -1 runs", {.items = 6000, .seed = 3, .max_run = 20, .anim_minus_percent = 60, .anim_change_percent = 40}, true});
  lists.push_back({"no technique before cuts", {.items = 6000, .seed = 4, .max_run = 6, .none_percent = 45}, true});
  lists.push_back({"seed breaking pattern", {.items = 4000, .seed = 5, .pattern = true}, true});
  lists.push_back({"max count flushes", {.items = 6000, .seed = 6, .max_run = 120, .max_count = 6}, true});
  lists.push_back({"last chunks without technique", {.items = 6000, .seed = 7, .max_run = 12, .enabled_percent = 10}, true});
  // No animation changes: every seed hits, so the chunks without a technique are replayed and copied back, not run again.
  lists.push_back({"last chunks without technique or animation", {.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10}, true});
  return lists;
}

static void BGame(dr::GameCalls* game);

static dr::GameCalls Game(bool usable) {
  dr::GameCalls game = {.loops = {{reinterpret_cast<uint32_t>(&SynthDepthLoop), reinterpret_cast<uint32_t>(&SynthColourLoop)}},
          .flush = reinterpret_cast<uint32_t>(&FakeFlush),
          .anim_call = reinterpret_cast<uint32_t>(&FakeAnimCall),
          .dirty_call = reinterpret_cast<uint32_t>(&FakeDirty),
          .view_call = reinterpret_cast<uint32_t>(&FakeView),
          .srv_call = reinterpret_cast<uint32_t>(&FakeSrv),
          .faction_call = reinterpret_cast<uint32_t>(&FakeFactionColours),
          .cached_constants = &cached_constants,
          .faction_default = &faction_default,
          .weighted_vtables = {{reinterpret_cast<uint32_t>(mesh_vtable.data())}},
          .usable = usable};
  BGame(&game);
  return game;
}

static void BSetup();

static void Setup() {
  mesh_vtable.fill(0);
  mesh_vtable[0x68 / 4] = reinterpret_cast<uint32_t>(&FakeVf68);
  foreign_vtable = mesh_vtable;
  foreign_vtable[0x64 / 4] = reinterpret_cast<uint32_t>(&FakeForeignVf64);
  node_vtable.fill(0);
  node_vtable[0xc0 / 4] = reinterpret_cast<uint32_t>(&FakeFactionLists);
  synth_node.vtable = node_vtable.data();
  // The first constant's four blocks and the second constant's block (state + 0x2d4, its cell pointer at +0x14 of it).
  for (uint32_t i = 0; i < const1_blocks.size(); ++i) const1_blocks[i].cell = &const1_cells[i];
  *reinterpret_cast<float**>(synth_node.state + 0x2e8) = &const2_cell;
  BSetup();
  g_shared = dr::SharedPage();
  REQUIRE(g_shared != nullptr);
  REQUIRE(g_shared->record == reinterpret_cast<uint32_t>(&dr::Record));
  REQUIRE(g_shared->dispatch_depth == reinterpret_cast<uint32_t>(&dr::DispatchDepth));
  REQUIRE(g_shared->dispatch_colour == reinterpret_cast<uint32_t>(&dr::DispatchColour));
  REQUIRE(g_shared->dispatch_mesh == reinterpret_cast<uint32_t>(&dr::DispatchMesh));
  dr::SetGame(Game(true));
  // Every traced call compares (a real trace compares only in its last frame); DepthTrace checks the real rule.
  di::controls.compare_every_call = true;
  // The controls' tests make compares differ on purpose: a latched group would run directly for the rest of the process.
  di::controls.latch_groups = false;
}

static void DepthStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  for (const ListCase& list : Lists()) {
    const auto world = BuildWorld(list.spec);
    const Outcome serial = RunSerial(world.get());
    CHECK(serial.log.size() > 100);
    for (const dr::Stage stage : RECORD_STAGES) {
      for (uint32_t k = 2; k <= 16; ++k) {
        for (const bool tracing : {false, true}) {
          const Outcome staged = RunStaged(world.get(), stage, k, tracing);
          char label[128];
          std::snprintf(label, sizeof(label), "%s, %s, K %u%s", list.name, StageName(stage), k, (tracing ? ", traced" : ""));
          const bool equal = Equal(serial, staged, label);
          CHECK(equal);
          CHECK(staged.portions > 2);
          CHECK(staged.direct_calls == staged.small_calls);
          CHECK(staged.accounted);
          if (list.expect_chunks && k >= 4) CHECK(staged.chunks >= 2 * 2);
          if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
          if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
          CHECK(!CompareDiffers(staged));
          reruns_seen += staged.reruns;
          ++cases;
        }
      }
    }
  }
  CHECK(reruns_seen > 0);
  std::printf("depth: %u cases (8 lists x 4 stages x K 2..16 x trace off / on, portions at begin != 0): logs, tracker (+0x50, +0x5c, +0x28, +0x69 included) and mesh fields equal the serial run; %u chunks run again\n", cases, reruns_seen);
}

static void DepthFlushEntry() {
  test_controls.entry_site = true;
  uint32_t cases = 0;
  for (const ListCase& list : Lists()) {
    const auto world = BuildWorld(list.spec);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (const uint32_t k : {4u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, list.name));
        CHECK(staged.ops > 0);
        ++cases;
      }
    }
  }
  test_controls.entry_site = false;
  std::printf("depth: the flush entry's site id (shared group) for every flush: %u cases equal\n", cases);
}

static void DepthTouchAhead() {
  const auto world = BuildWorld({.items = 6000, .seed = 11, .max_run = 20});
  const Outcome serial = RunSerial(world.get());
  for (uint32_t k = 2; k <= 16; ++k) {
    const uint32_t before = di::touch_generation.load();
    const Outcome staged = RunStaged(world.get(), dr::Stage::TOUCH_AHEAD, k);
    CHECK(Equal(serial, staged, "touch-ahead"));
    CHECK(staged.direct_calls == staged.small_calls);
    CHECK(di::descriptor.workers == k - 1);
    CHECK(di::touch_generation.load() == before + (staged.portions - staged.small_calls));
  }
  std::printf("depth: touch-ahead at K 2..16: the loop unchanged, the helpers stopped after each call\n");
}

static void DepthOverflow() {
  const auto world = BuildWorld({.items = 6000, .seed = 12, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  uint32_t reruns = 0;
  for (const uint32_t limit : {200u, 1000u, 4000u}) {
    di::op_buffer_limit = limit;
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (uint32_t k : {2u, 5u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, "tiny buffers"));
        reruns += staged.reruns;
        CHECK(staged.reruns > 0);
        CHECK(!CompareDiffers(staged));
      }
    }
  }
  di::op_buffer_limit = di::OP_ARENA_BYTES;
  std::printf("depth: tiny op buffers: overflowing chunks run again, logs equal (%u reruns)\n", reruns);
}

// One call of 200000 items: chunks larger than the op buffer's reserve.
static void DepthLarge() {
  const auto world = BuildWorld({.items = 200000, .seed = 41, .max_run = 8, .max_count = 6, .portion_numerator = 12000000});
  const Outcome serial = RunSerial(world.get());
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 3u}) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "large call"));
      CHECK(staged.portions == 2 && staged.direct_calls == 0);
      CHECK(staged.accounted);
      CHECK(staged.most_buffer <= di::OP_ARENA_BYTES);
    }
  }
  CHECK(di::op_committed <= di::OP_ARENA_BYTES);
  std::printf("depth: 200000 items in one call at K 2 / 3: logs equal, op memory within its %u MB reserve (%u KB committed)\n",
              di::OP_ARENA_BYTES >> 20, di::op_committed >> 10);
}

// A call that starts in a state the chunks can't reproduce runs directly.
static void DepthEntryState() {
  const auto world = BuildWorld({.items = 6000, .seed = 16, .max_run = 10});
  for (const uint32_t variant : {0u, 1u}) {
    world->pending = (variant == 0 ? 5 : 0);
    world->change_mask = (variant == 1 ? 8 : 0);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      const Outcome staged = RunStaged(world.get(), stage, 8, true);
      CHECK(Equal(serial, staged, (variant == 0 ? "pending instances" : "change mask")));
      CHECK(staged.first_reason == "entry state");
      CHECK(staged.direct_calls > staged.small_calls);
    }
  }
  world->pending = 0;
  world->change_mask = 0;
  std::printf("depth: a call that starts with pending instances or a change mask runs directly (entry state), logs equal\n");
}

// The first mesh of the call is the tracker's last one, with state another view wrote and that recurs later in the list.
static void DepthContinuation() {
  const auto world = BuildWorld({.items = 6000, .seed = 17, .max_run = 10, .max_count = 6, .continuation = true});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (uint32_t k = 2; k <= 16; ++k) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "continuation"));
      CHECK(staged.first_reason == "continuation");
      ++cases;
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  // Guard off: the compare run has set the mesh up before chunk 0's snapshot is taken, so the replay flushes it with the wrong state.
  di::controls.guard_continuation = false;
  const Outcome unguarded = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  di::controls.guard_continuation = true;
  CHECK(!Equal(serial, unguarded, "control: continuation guard off", false));
#endif
  std::printf("depth: a continuation of foreign mesh state runs directly (%u cases equal)\n", cases);
}

// The traced calls of the last staged run that went direct for `reason`.
static size_t TracedReasons(std::string_view reason) {
  return static_cast<size_t>(std::ranges::count_if(
      di::trace.calls, [&](const di::CallTrace& call) { return std::string_view(call.direct_reason) == reason; }));
}

static void DepthFallbacks() {
  const auto world = BuildWorld({.items = 6000, .seed = 13, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  // Fewer than 512 items.
  const auto few_items = BuildWorld({.items = 300, .seed = 14, .max_run = 10});
  const Outcome few_serial = RunSerial(few_items.get());
  const Outcome few_staged = RunStaged(few_items.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(few_serial, few_staged, "small call"));
  CHECK(few_staged.direct_calls == few_staged.portions && few_staged.portions == 2);
  // The stage Direct and the row off.
  for (const dr::Stage stage : {dr::Stage::DIRECT, dr::Stage::OFF}) {
    const Outcome staged = RunStaged(world.get(), stage, 8);
    CHECK(Equal(serial, staged, "stage direct"));
    CHECK(staged.direct_calls == staged.portions);
  }
  // The pool smaller than the stage needs.
  UpdateAt(1, {.stage = dr::Stage::OFF});
  RunStaged(world.get(), dr::Stage::WORKERS, 4);
  CHECK(wi::created == 3);
  const Outcome missing = RunStaged(world.get(), dr::Stage::ONE_WORKER, 1);
  CHECK(Equal(serial, missing, "pool missing"));
  CHECK(missing.direct_calls == missing.portions);
  // Workers and Touch-ahead with no worker on offer (a limit of 1, a pool that could not start): every call that is not small runs
  // directly, "pool missing".
  for (const dr::Stage stage : {dr::Stage::WORKERS, dr::Stage::TOUCH_AHEAD}) {
    for (const bool failed_pool : {false, true}) {
      UpdateAt(1, {.stage = dr::Stage::OFF});
      wi::limits[static_cast<size_t>(wp::User::DRAW_RECORDING)] = (failed_pool ? wp::MAX_WORKERS + 1 : 1);
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
      wi::controls.fail_thread_creation = failed_pool;
#endif
      const Outcome empty = RunStaged(world.get(), stage, 8, true);
      CHECK(Equal(serial, empty, "empty pool"));
      CHECK(wp::Available(wp::User::DRAW_RECORDING) == 0 && empty.direct_calls == empty.portions);
      CHECK(TracedReasons("pool missing") == empty.portions - empty.small_calls);
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
      wi::controls.fail_thread_creation = false;
#endif
      wi::limits[static_cast<size_t>(wp::User::DRAW_RECORDING)] = wp::MAX_WORKERS + 1;
    }
  }
  UpdateAt(1, {.stage = dr::Stage::OFF});
  // Another thread.
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 1});
  ResetWorld(world.get());
  Outcome other_outcome;
  std::thread other([&] { other_outcome = RunCalls(world.get(), false); });
  other.join();
  CHECK(other_outcome.direct_calls == other_outcome.portions);
  CHECK(Equal(serial, other_outcome, "other thread"));
  // A loop group that is off.
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 2});
  ResetWorld(world.get());
  const Outcome loop_off = RunCalls(world.get(), false);
  CHECK(loop_off.direct_calls == loop_off.portions);
  CHECK(Equal(serial, loop_off, "loop off"));
  // Not usable.
  dr::SetGame(Game(false));
  const Outcome unusable = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, unusable, "not usable"));
  CHECK(unusable.direct_calls == unusable.portions);
  dr::SetGame(Game(true));
  std::printf("depth: fall-backs (small call, Direct, Off, pool missing, other thread, loop off, not usable) run the loop directly, logs equal\n");
}

static void DepthTrace() {
  const auto world = BuildWorld({.items = 6000, .seed = 15, .max_run = 12});
  // The trace armed through the environment: written next to the exe after its frames.
  const std::filesystem::path file = std::filesystem::path(__argv[0]).parent_path() / "tw_draw_recording.txt";
  std::filesystem::remove(file);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", "0,3");
  di::trace.read_environment = false;
  di::trace.armed = false;
  di::trace.finished = false;
  di::controls.compare_every_call = false;
  ResetWorld(world.get());
  const dr::Settings settings = {.stage = dr::Stage::WORKERS, .loops = 1};
  UpdateAt(6, settings);
  CHECK(di::trace.armed);
  UpdateAt(6, settings);
  CHECK(di::trace.active);
  // Frames 1 and 2 of the trace are timed (no compare, no compare slot); the last one compares.
  for (int frame = 0; frame < 2; ++frame) {
    const size_t calls_before = di::trace.calls.size();
    Driver(world.get(), [&](uint32_t begin, uint32_t end) { dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, begin, end); });
    CHECK(di::trace.calls.size() > calls_before);
    for (size_t i = calls_before; i < di::trace.calls.size(); ++i) {
      const di::CallTrace& call = di::trace.calls[i];
      CHECK(call.compared == (frame == 1));
      CHECK((call.compare != 0) == (frame == 1 && call.chunks > 1));
      CHECK(call.run_scan != 0);
      CHECK((call.compare_result == di::CompareResult::NONE) == !(frame == 1 && call.chunks > 1));
      CHECK(call.compare_result != di::CompareResult::DIFFERS);
    }
    UpdateAt(6, settings);
  }
  di::controls.compare_every_call = true;
  CHECK(!di::trace.active && di::trace.finished);
  std::ifstream stream(file, std::ios::binary);
  const std::string text_value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(text_value.find("compare: equal") != std::string::npos);
  CHECK(text_value.find("compare: not run (timing frame)") != std::string::npos);
  CHECK(text_value.find(" timing\r\n") != std::string::npos && text_value.find(" compared\r\n") != std::string::npos);
  CHECK(text_value.find("frame:") != std::string::npos);
  CHECK(text_value.find("K 6") != std::string::npos);
  CHECK(text_value.find("chunk items:") != std::string::npos);
  CHECK(text_value.find("group 0 family -") != std::string::npos);
  CHECK(text_value.find("const ") != std::string::npos && text_value.find("srv ") != std::string::npos && text_value.find("faction ") != std::string::npos);
  CHECK(text_value.find("differs") == std::string::npos);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", nullptr);
  stream.close();
  std::filesystem::remove(file);
  std::printf("depth: trace armed from the environment wrote the file (group, K, chunk sizes), compare equal\n");
}

// The Record rules the thunks rely on, called directly.
static void RecordRules() {
  static dr::Slot unit;
  static std::array<uint32_t, 64> buffer;
  static SynthMesh mesh;
  mesh = {};
  mesh.view_tag = 0x40;
  mesh.pass = 0x44;
  const auto reset = [&](uint32_t group, uint32_t mode) {
    unit = {};
    unit.group = group;
    unit.mode = mode;
    unit.ops_begin = unit.ops_cursor = buffer.data();
    unit.ops_end = buffer.data() + buffer.size();
    buffer.fill(0);
  };
  const auto flush = [&](dr::Site site, uint32_t count) {
    StackImage image = MakeImage(site, reinterpret_cast<uint32_t>(&mesh));
    image.stack[5] = count;
    dr::Record(&unit, &image.frame);
  };
  CHECK(di::FLUSH_PAYLOAD == 14);
  // A site of another loop group in the chunk: run again, nothing recorded.
  reset(1, dr::MODE_RECORD);
  StackImage vf68 = MakeImage(dr::Site::DEPTH_VF68, reinterpret_cast<uint32_t>(&mesh));
  dr::Record(&unit, &vf68.frame);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0 && unit.ops_cursor == unit.ops_begin);
  // A site id past the table.
  reset(0, dr::MODE_RECORD);
  StackImage unknown = MakeImage(static_cast<dr::Site>(static_cast<uint32_t>(dr::Site::COUNT) + 3), 0);
  dr::Record(&unit, &unknown.frame);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  // The flush entry (shared group) records in any loop group, with the any byte.
  reset(1, dr::MODE_RECORD);
  unit.tracker[0x69] = 1;
  flush(dr::Site::FLUSH_ENTRY, 3);
  CHECK(unit.flags == 0 && unit.ops_cursor == unit.ops_begin + 1 + di::FLUSH_PAYLOAD);
  CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::FLUSH) && (buffer[0] >> 16) == 1 + di::FLUSH_PAYLOAD);
  CHECK(buffer[1 + 13] == 1 && unit.tracker[0x69] == 0 && buffer[1 + 11] == 0x40 && buffer[1 + 12] == 0x44);
  // Suppress mode: the byte is cleared, nothing recorded.
  reset(0, dr::MODE_SUPPRESS);
  unit.tracker[0x69] = 1;
  flush(dr::Site::DEPTH_FLUSH_END, 3);
  CHECK(unit.tracker[0x69] == 0 && unit.ops_cursor == unit.ops_begin && unit.flags == 0);
  // A flush of no instances leaves the byte alone, in both modes.
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    reset(0, mode);
    unit.tracker[0x69] = 1;
    flush(dr::Site::DEPTH_FLUSH_END, 0);
    CHECK(unit.tracker[0x69] == 1 && unit.ops_cursor == unit.ops_begin);
  }
  // A full buffer: flagged, and the byte is cleared all the same.
  reset(0, dr::MODE_RECORD);
  unit.ops_end = unit.ops_begin + 3;
  unit.tracker[0x69] = 1;
  flush(dr::Site::DEPTH_FLUSH_END, 3);
  CHECK((unit.flags & dr::SLOT_OVERFLOW) != 0 && unit.tracker[0x69] == 0);
  // The animation site keeps the index in both modes.
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    reset(0, mode);
    StackImage anim = MakeImage(dr::Site::DEPTH_ANIM, 0x1000);
    anim.stack[9] = 5;
    dr::Record(&unit, &anim.frame);
    CHECK(unit.anim == 5 && (unit.ops_cursor != unit.ops_begin) == (mode == dr::MODE_RECORD));
  }
  std::printf("record: group check, unknown site, flush entry with the any byte (record, suppress, count 0, overflow), animation index\n");
}

// Each control makes one thing wrong; its run must differ (or the compare must report it).
static void DepthControls() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  const auto world = BuildWorld({.items = 6000, .seed = 21, .max_run = 8});
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "control baseline"));

  const auto pattern = BuildWorld({.items = 4000, .seed = 22, .pattern = true});
  const Outcome pattern_serial = RunSerial(pattern.get());
  // With validation on the pattern list is run again at its misses; off, the run differs.
  const Outcome validated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  CHECK(Equal(pattern_serial, validated, "pattern validated"));
  CHECK(validated.reruns > 0);
  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(pattern_serial, unvalidated, "control: validation off", false));

  test_controls.drop_end_flush = true;
  const Outcome dropped = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  test_controls.drop_end_flush = false;
  CHECK(!Equal(serial, dropped, "control: dropped flush", false));

  di::controls.translate_pointers = false;
  const Outcome untranslated = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.translate_pointers = true;
  CHECK(!Equal(serial, untranslated, "control: translation off", false));

  test_controls.anim_stack = 8;
  const Outcome wrong_anim = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  test_controls.anim_stack = 9;
  CHECK(!Equal(serial, wrong_anim, "control: wrong anim stack slot", false));

  // The any byte the loop sets itself must travel in the flush op.
  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: any capture off", false));

  // The stride of the last chunk that wrote one, not the last chunk's.
  const auto tail = BuildWorld({.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10});
  const Outcome tail_serial = RunSerial(tail.get());
  di::controls.stride_non_zero = false;
  const Outcome stale = RunStaged(tail.get(), dr::Stage::RECORDED, 8);
  di::controls.stride_non_zero = true;
  CHECK(!Equal(tail_serial, stale, "control: stride copied always", false));
  CHECK(Equal(tail_serial, RunStaged(tail.get(), dr::Stage::RECORDED, 8), "control: stride rule on"));

  // An op in one chunk that the serial run does not have: the log differs and the compare names it.
  test_controls.inject_chunk = 2;
  const Outcome injected = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  test_controls.inject_chunk = -1;
  CHECK(!Equal(serial, injected, "control: injected op", false));
  CHECK(CompareDiffers(injected));

  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "controls cleared"));
  std::printf("controls: validation off, dropped flush, translation off, wrong anim slot, any capture off, stride copied always, injected op: each run differs (injection named by the compare)\n");
#else
  std::printf("controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// A dispatcher call inside a round: a real flush the replay makes calls the dispatcher with a list of another world and tracker.
// That call runs directly (S-2), as the serial run's does; without the guard it takes the slots the round is replaying.
static void NestedWorldRun() {
  ResetState(nest_a_world_pointer);
  Driver(nest_a_world_pointer, [&](uint32_t begin, uint32_t end) { DispatchPortion(nest_a_world_pointer, begin, end); });
}

static void DepthNested() {
  const auto world = BuildWorld({.items = 6000, .seed = 41, .max_run = 10});
  const auto nested = BuildWorld({.items = 1500, .seed = 42, .max_run = 10});
  nest_a_world_pointer = nested.get();
  nest_a_call = NestedWorldRun;
  UpdateAt(1, {.stage = dr::Stage::OFF});
  nest_a_every = 40;
  nest_a_flushes = 0;
  nest_a_budget = 3;
  const Outcome serial = RunSerial(world.get());
  CHECK(nest_a_budget == 0 && !serial.log.empty());
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t k : {4u, 8u}) {
      nest_a_flushes = 0;
      nest_a_budget = 3;
      di::nested_calls = 0;
      const Outcome staged = RunStaged(world.get(), stage, k);
      CHECK(Equal(serial, staged, "A nested dispatcher calls"));
      CHECK(nest_a_budget == 0 && staged.replayed > 0 && !di::in_round);
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
      CHECK(di::nested_calls >= 3);
#endif
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  // The guard alone: a call made while a round stands (the flag set by hand, no round to corrupt) runs directly, and without the
  // guard it records. (Nested inside a real replay the unguarded call overwrites the ops the outer round is reading.)
  nest_a_every = 0;
  for (const bool guard : {true, false}) {
    di::controls.nested_guard = guard;
    di::nested_calls = 0;
    di::in_round = true;
    const Outcome inside = RunStaged(nested.get(), dr::Stage::RECORDED, 4);
    di::in_round = false;
    di::controls.nested_guard = true;
    CHECK(Equal(RunSerial(nested.get()), inside, "A nesting guard"));
    CHECK(guard ? (inside.direct_calls == inside.portions && di::nested_calls == inside.portions && inside.replayed == 0)
                : (inside.replayed > 0 && di::nested_calls == 0));
  }
#endif
  nest_a_every = 0;
  nest_a_budget = 0;
  nest_a_call = nullptr;
  std::printf("depth nested: a dispatcher call from a replayed flush runs directly, logs equal the serial run; with the guard off a call inside a round records\n");
}

// N-1: a recorded op buffer with a zero header is never walked: the call runs directly, logs equal (it would loop forever).
static void DepthBadOps() {
  const auto world = BuildWorld({.items = 6000, .seed = 43, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t chunk : {1u, 2u, 3u}) {
      test_controls.corrupt_chunk = static_cast<int>(chunk);
      const Outcome staged = RunStaged(world.get(), stage, 4, true);
      CHECK(test_controls.corrupt_chunk == -1);
      test_controls.corrupt_chunk = -1;
      CHECK(Equal(serial, staged, "A damaged op buffer"));
      CHECK(staged.first_reason == "bad ops" && staged.replayed > 0);
    }
  }
  std::printf("depth bad ops: a zero op header in one chunk makes that call run directly, logs equal the serial run\n");
}

// The chunk end's join of the "any" byte (+0x69) into the real tracker: a chunk that left nothing pending keeps what is there
// (a body never ends a mesh's run with it set after its flush, so no list shows the difference: a unit on the replay).
static void AnyJoinControl() {
  dr::Slot unit = {};
  std::array<uint32_t, 8> buffer = {};
  unit.ops_begin = buffer.data();
  unit.ops_cursor = buffer.data();
  unit.ops_end = buffer.data() + buffer.size();
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> real = {};
  for (const uint8_t pending : {uint8_t{0}, uint8_t{1}}) {
    for (const uint8_t there : {uint8_t{0}, uint8_t{1}}) {
      real = {};
      real[di::ANY_OFFSET] = there;
      unit.tracker[di::ANY_OFFSET] = pending;
      di::Replay replay = {.real = real.data(), .group_index = static_cast<uint32_t>(dr::Group::DEPTH_A), .last_anim = -1, .op_count = 0,
                           .trace_call = nullptr};
      di::ReplayChunk(&replay, &unit);
      CHECK(real[di::ANY_OFFSET] == (pending | there));
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
      real[di::ANY_OFFSET] = there;
      di::controls.any_join = false;
      di::ReplayChunk(&replay, &unit);
      di::controls.any_join = true;
      CHECK(real[di::ANY_OFFSET] == pending);
#endif
    }
  }
  std::printf("any join: a chunk's pending +0x69 joins the real byte (a copy over it loses what an earlier chunk left)\n");
}

// ---------------------------------------------------------------- part 3: the colour path

static std::vector<ListCase> ColourLists() {
  std::vector<ListCase> lists;
  lists.push_back({"colour short runs", {.items = 6000, .seed = 51, .max_run = 8, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour runs 1 to 300", {.items = 9000, .seed = 52, .max_run = 300, .none_percent = 15, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour animation -1 runs",
                   {.items = 6000, .seed = 53, .max_run = 20, .anim_minus_percent = 60, .anim_change_percent = 40, .colour = true, .unique_meshes = true},
                   true});
  lists.push_back({"colour no technique", {.items = 6000, .seed = 54, .max_run = 6, .none_percent = 45, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour max count flushes", {.items = 6000, .seed = 55, .max_run = 120, .max_count = 6, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour many variations", {.items = 6000, .seed = 56, .max_run = 30, .colour = true, .unique_meshes = true, .variation_percent = 70}, true});
  lists.push_back({"colour any-only items", {.items = 6000, .seed = 57, .max_run = 12, .colour = true, .unique_meshes = true, .any_only_percent = 20}, true});
  lists.push_back({"colour last chunks without technique",
                   {.items = 6000, .seed = 58, .max_run = 12, .enabled_percent = 10, .colour = true, .unique_meshes = true}, true});
  lists.push_back({"colour constants only",
                   {.items = 6000, .seed = 59, .max_run = 10, .colour = true, .unique_meshes = true, .const1_percent = 100, .const2_percent = 100,
                    .texture_percent = 0, .faction_percent = 0},
                   true});
  lists.push_back({"colour constants merge across chunks",
                   {.items = 6000, .seed = 60, .max_run = 6, .anim_change_percent = 0, .colour = true, .unique_meshes = true, .merge = true}, true});
  return lists;
}

static const std::array<uint32_t, 8> COLOUR_K = {2, 3, 4, 5, 6, 8, 11, 16};

static void ColourStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  uint32_t split_seen = 0;
  uint32_t continued_seen = 0;
  uint32_t repeats_seen = 0;
  std::array<uint32_t, 5> miss_fields = {};
  std::array<uint32_t, static_cast<size_t>(di::OpKind::COUNT)> kinds = {};
  for (const ListCase& list : ColourLists()) {
    const auto world = BuildWorld(list.spec);
    for (const uint8_t cache_flag : {uint8_t{0}, uint8_t{1}}) {
      cached_constants = cache_flag;
      const Outcome serial = RunSerial(world.get());
      CHECK(serial.log.size() > 100);
      for (const dr::Stage stage : RECORD_STAGES) {
        for (const uint32_t k : COLOUR_K) {
          for (const bool tracing : {false, true}) {
            const Outcome staged = RunStaged(world.get(), stage, k, tracing);
            char label[160];
            std::snprintf(label, sizeof(label), "%s, cache flag %u, %s, K %u%s", list.name, cache_flag, StageName(stage), k, (tracing ? ", traced" : ""));
            CHECK(Equal(serial, staged, label));
            CHECK(staged.portions > 2);
            CHECK(staged.direct_calls == staged.small_calls);
            CHECK(staged.accounted);
            CHECK(staged.conflicts == 0);
            if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
            if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
            CHECK(!CompareDiffers(staged));
            CHECK(staged.misses_explained);
            reruns_seen += staged.reruns;
            split_seen += staged.split_constants;
            continued_seen += staged.continued;
            repeats_seen += staged.constant_repeats;
            for (size_t field = 0; field < miss_fields.size(); ++field) miss_fields[field] += staged.miss_fields[field];
            for (size_t kind = 0; kind < kinds.size(); ++kind) kinds[kind] += staged.kind_ops[kind];
            ++cases;
          }
        }
      }
    }
  }
  cached_constants = 0;
  CHECK(reruns_seen > 0);
  // Calls that continue the tracker's mesh with state the guard accepts are recorded and replayed (at portion boundaries).
  CHECK(continued_seen > 0);
  // Seed misses by field: the loop changes the faction, the mask, the textures and the animation, never the flags (S-3).
  CHECK(miss_fields[0] > 0 && miss_fields[1] > 0 && miss_fields[2] > 0 && miss_fields[3] == 0 && miss_fields[4] > 0);
  // Every constant op repeats at least once, and equal constants in a row merge (repeat counts above 1).
  CHECK(repeats_seen > kinds[static_cast<size_t>(di::OpKind::CONSTANT)]);
  for (const di::OpKind kind : {di::OpKind::FLUSH, di::OpKind::VF68, di::OpKind::ANIM, di::OpKind::CONSTANT, di::OpKind::SRV, di::OpKind::FACTION}) {
    CHECK(kinds[static_cast<size_t>(kind)] > 0);
  }
  CHECK(split_seen > 0);
  std::printf("colour: %u cases (10 lists x cache flag 0 / 1 x 4 stages x 8 K x trace off / on): logs, tracker, mesh fields and constant cells equal the serial run; %u chunks run again; ops flush %u vf68 %u anim %u const %u srv %u faction %u; %u constants split by a chunk end\n",
              cases, reruns_seen, kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], split_seen);
  std::printf("colour: %u calls continued the tracker's mesh and were replayed; seed misses by field (traced): faction %u decals %u mask %u flags %u anim %u; constant repeats %u\n",
              continued_seen, miss_fields[0], miss_fields[1], miss_fields[2], miss_fields[3], miss_fields[4], repeats_seen);
}

// Meshes that recur in several chunks: the call runs directly (before any replay), and with the owner table off a write of
// another chunk landing before a snapshot makes the run differ.
static void ColourInterleaved() {
  uint32_t cases = 0;
  // (No animation changes, textures or faction blocks: the seeds hit, so the chunks of the control are replayed, not run again.)
  for (const Spec& spec : {Spec{.items = 6000, .seed = 61, .max_run = 8, .none_percent = 0, .anim_change_percent = 0, .portion_numerator = 12000000,
                                .colour = true, .texture_percent = 0, .faction_percent = 0},
                           Spec{.items = 6000, .seed = 62, .max_run = 8, .none_percent = 0, .anim_change_percent = 0, .portion_numerator = 12000000,
                                .colour = true, .unique_meshes = true, .repeat_far = true, .texture_percent = 0, .faction_percent = 0}}) {
    const auto world = BuildWorld(spec);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
      for (const uint32_t k : {2u, 4u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, "interleaved meshes"));
        CHECK(staged.portions == 2 && staged.conflicts == 2);
        ++cases;
      }
    }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    // The owner table off and another chunk's write racing in before the snapshots of chunks 1 to 3: wrong.
    di::controls.race_chunks = 0xe;
    CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 4, true), "owner table on, race hook on"));
    di::controls.owner_check = false;
    const Outcome raced = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
    di::controls.owner_check = true;
    di::controls.race_chunks = 0;
    CHECK(!Equal(serial, raced, "control: owner table off", false));
    CHECK(raced.conflicts == 0);
#endif
  }
  std::printf("colour: meshes recurring in several chunks: %u cases run directly (both portions of each pass), logs equal; owner table off + a racing write differs\n", cases);
}

// A mesh class the census did not cover: its chunk is run again.
static void ColourUnknownClass() {
  const auto world = BuildWorld({.items = 6000, .seed = 64, .max_run = 8, .colour = true, .unique_meshes = true, .foreign_percent = 10});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 4u, 8u, 16u}) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "unknown class"));
      CHECK(!CompareDiffers(staged));
      if (stage != dr::Stage::CHUNKED) CHECK(staged.reruns > 0);
      ++cases;
    }
  }
  std::printf("colour: meshes of an unknown class (10%% of the meshes): the chunks that hold one run again, logs equal (%u cases, the compare says not comparable)\n", cases);
}

// The tracker's first mesh is the last call's, with another view's state, and returns later in the first chunk.
static void ColourContinuation() {
  const auto world = BuildWorld({.items = 6000, .seed = 63, .max_run = 10, .max_count = 6, .continuation = true, .colour = true, .unique_meshes = true});
  const Outcome serial = RunSerial(world.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (uint32_t k = 2; k <= 16; ++k) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "colour continuation"));
      CHECK(staged.first_reason == "continuation");
      ++cases;
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  // With the guard off the variation path's flush (it writes the mesh's +0x40, not run while recording) would be missing before
  // the shared-state call that reads it: the replay's snapshot is the foreign +0x40.
  // (Not traced: the compare run's setup of the returning mesh would write the flush's value into it by chance.)
  di::controls.guard_continuation = false;
  const Outcome unguarded = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.guard_continuation = true;
  CHECK(!Equal(serial, unguarded, "control: colour continuation guard off", false));
#endif
  std::printf("colour: a continuation of foreign mesh state runs directly (%u cases equal)\n", cases);
}

// A continuation (no setup site before the call's first item) of a class the census did not cover runs directly; so does a
// warm-up item that continues such a mesh (its chunk is run again). The class's virtual call at a batch start logs when it runs:
// made while recording or warming up, it lands before the replayed ops in the log.
static void ColourForeignContinuation() {
  const Spec spec = {.items = 6000, .seed = 91, .max_run = 8, .portion_numerator = 12000000, .colour = true, .unique_meshes = true};
  const auto first = BuildContinuedWorld(spec, 40);
  first->start_mesh->vtable = foreign_vtable.data();
  first->items[0].variation = 2;
  const Outcome first_serial = RunSerial(first.get());
  CHECK(std::ranges::any_of(first_serial.log, [](const LogEntry& entry) { return entry[0] == 7; }));
  // The warm-up item before the first cut (K = 2: item 3049) is the tracker's foreign mesh, which the call reaches later.
  const auto warm = BuildContinuedWorld(spec, 0);
  warm->start_mesh->vtable = foreign_vtable.data();
  for (uint32_t i = 2900; i < 3050; ++i) warm->items[i].mesh = warm->start_mesh;
  const Outcome warm_serial = RunSerial(warm.get());
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 4u, 16u}) {
      const Outcome staged = RunStaged(first.get(), stage, k, true);
      CHECK(Equal(first_serial, staged, "foreign continuation"));
      CHECK(staged.first_reason == "unknown class");
      ++cases;
    }
    for (const bool tracing : {false, true}) {
      CHECK(Equal(warm_serial, RunStaged(warm.get(), stage, 2, tracing), "foreign warm-up item"));
      ++cases;
    }
  }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  di::controls.check_continuation_class = false;
  const Outcome unchecked_call = RunStaged(first.get(), dr::Stage::RECORDED, 4);
  const Outcome unchecked_warm = RunStaged(warm.get(), dr::Stage::RECORDED, 2);
  di::controls.check_continuation_class = true;
  CHECK(!Equal(first_serial, unchecked_call, "control: continuation class check off (call)", false));
  CHECK(!Equal(warm_serial, unchecked_warm, "control: continuation class check off (warm-up)", false));
#endif
  std::printf("colour: a continuation of a mesh of an unknown class runs directly, a warm-up item that continues one is skipped (chunk run again): %u cases equal; check off: both runs differ\n", cases);
}

// The call-start claim of chunk 0's first mesh: chunk 0 continues M (no setup site to claim it) and a later chunk sets M up.
static void ColourClaimControl() {
  const Spec spec = {.items = 6000, .seed = 92, .max_run = 8, .anim_change_percent = 0, .portion_numerator = 12000000, .colour = true,
                     .unique_meshes = true, .variation_percent = 0, .texture_percent = 0, .faction_percent = 0};
  const auto world = BuildContinuedWorld(spec, 3100);
  // The only variation of chunk 0's mesh is its last item's: a variation makes the next item a "change" (a setup that claims
  // the mesh), so this is the one shared-state call that chunk 0 makes before it could have claimed M any other way.
  world->items[3099].variation = 2;
  for (uint32_t i = 4000; i < 4010; ++i) world->items[i].mesh = world->start_mesh;
  const Outcome serial = RunSerial(world.get());
  // Both passes conflict (pass 2 starts elsewhere: chunk 0's setup of M claims it).
  const Outcome claimed = RunStaged(world.get(), dr::Stage::RECORDED, 2, true);
  CHECK(Equal(serial, claimed, "claimed"));
  CHECK(claimed.conflicts == 2);
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  di::controls.race_chunks = 1;
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 2, true), "claim on, race hook on"));
  di::controls.claim_first_mesh = false;
  // Without the claim only pass 2 conflicts; the other chunk's setup landing before chunk 0's snapshots (the hook) is wrong.
  di::controls.race_chunks = 0;
  const Outcome unclaimed = RunStaged(world.get(), dr::Stage::RECORDED, 2, true);
  CHECK(unclaimed.conflicts == 1);
  di::controls.race_chunks = 1;
  const Outcome raced = RunStaged(world.get(), dr::Stage::RECORDED, 2);
  di::controls.race_chunks = 0;
  di::controls.claim_first_mesh = true;
  CHECK(Equal(serial, unclaimed, "claim off, no race"));
  CHECK(!Equal(serial, raced, "control: call-start claim off", false));
#endif
  std::printf("colour: chunk 0's continuation mesh is claimed at the call start: a later chunk's setup of it makes the call run directly; claim off + a racing setup differs\n");
}

// The warm-up puts back the mesh state it wrote: chunk 0 continues M past the first cut, the warm-up item (the last of M) takes
// the variation path, and a flush that reads the mesh state (strict) sees the warm-up's write if it is not put back.
static void ColourWarmUpRestore() {
  const Spec spec = {.items = 6000, .seed = 93, .max_run = 8, .anim_change_percent = 0, .max_count = 6, .portion_numerator = 12000000,
                     .colour = true, .unique_meshes = true, .variation_percent = 0, .const1_percent = 0, .const2_percent = 0,
                     .texture_percent = 0, .faction_percent = 0};
  const auto world = BuildContinuedWorld(spec, 3100);
  world->items[3099].variation = 2;
  world->strict = true;
  const Outcome serial = RunSerial(world.get());
  // (Not traced: the compare run's own writes would hide it.)
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 2), "warm-up restore on"));
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  di::controls.restore_warm_up = false;
  const Outcome unrestored = RunStaged(world.get(), dr::Stage::RECORDED, 2);
  di::controls.restore_warm_up = true;
  CHECK(!Equal(serial, unrestored, "control: warm-up restore off", false));
#endif
  std::printf("colour: the warm-up puts back the mesh state it wrote (strict flush): restore off differs\n");
}

// The replay's own constants, textures and faction block against the game's (written separately from the listing).
static void ColourReplayUnits() {
  const auto world = BuildWorld({.items = 10, .seed = 70, .colour = true});
  const uint32_t block = reinterpret_cast<uint32_t>(&const1_blocks[0]);
  uint32_t cases = 0;
  const auto run = [&](auto&& game, auto&& replay, auto&& setup) {
    setup();
    game_log.clear();
    game();
    const std::vector<LogEntry> expected = game_log;
    const uint32_t expected_cell = std::bit_cast<uint32_t>(const1_cells[0]);
    setup();
    game_log.clear();
    replay();
    CHECK(game_log == expected);
    CHECK(std::bit_cast<uint32_t>(const1_cells[0]) == expected_cell);
    ++cases;
  };
  for (const uint8_t flag : {uint8_t{0}, uint8_t{1}}) {
    for (const uint32_t cell_bits : {0x3f800000u, 0x00000000u, 0x80000000u, 0x7fc00000u}) {
      for (const uint32_t value_bits : {0x00000000u, 0x80000000u, 0x7fc00000u, 0x3f800000u}) {
        for (const uint32_t repeat : {1u, 3u}) {
          const auto setup = [&] {
            cached_constants = flag;
            const1_cells[0] = std::bit_cast<float>(cell_bits);
          };
          run([&] { for (uint32_t i = 0; i < repeat; ++i) GameConst(block, value_bits); }, [&] { di::ReplayConst(block, value_bits, repeat); }, setup);
        }
      }
    }
  }
  cached_constants = 0;
  for (const uint32_t texture : {0u, 5u}) {
    run([&] { GameSrv(0x1234, texture); }, [&] { di::ReplaySrv(0x1234, texture); }, [] {});
  }
  for (const int32_t faction : {-1, 0, 1, 2, 3}) {
    for (const SynthMask* mask : std::initializer_list<const SynthMask*>{nullptr, &world->masks[0], &world->masks[1], &world->masks[2]}) {
      run([&] { GameFaction(faction, mask, &synth_node, 0x5678); },
          [&] { di::ReplayFaction(faction, reinterpret_cast<uint32_t>(mask), reinterpret_cast<uint32_t>(&synth_node), 0x5678); }, [] {});
    }
  }
  // N-7: a node whose list call writes nothing through its out pointer: no list, nothing sent, nothing read through the pointer
  // (the -1 faction does not ask the node: its default colours are still sent).
  silent_vtable[0xc0 / 4] = reinterpret_cast<uint32_t>(&FakeFactionListsSilent);
  silent_node = silent_vtable.data();
  for (const int32_t faction : {0, 1, 2, 3}) {
    for (const SynthMask* mask : std::initializer_list<const SynthMask*>{nullptr, &world->masks[0]}) {
      game_log.clear();
      di::ReplayFaction(faction, reinterpret_cast<uint32_t>(mask), reinterpret_cast<uint32_t>(&silent_node), 0x5678);
      CHECK(game_log.empty());
    }
  }
  game_log.clear();
  di::ReplayFaction(-1, 0, reinterpret_cast<uint32_t>(&silent_node), 0x5678);
  CHECK(game_log.size() == 1);
  std::printf("colour: the replay of constants (flag 0 / 1, cell 1 / 0 / -0 / NaN, value 0 / -0 / NaN / 1, repeat 1 / 3), textures and the faction block (-1 and the lists of 6 / 3 / 70 / 4 colours, with and without a mask) equals the game's: %u cases; a node whose list call writes no list sends nothing (N-7)\n", cases);
}

// The colour sites in Record, called directly.
static void ColourRecordRules() {
  static dr::Slot unit;
  static std::array<uint32_t, 128> buffer;
  static SynthMesh mesh;
  static SynthMesh foreign;
  mesh = {};
  mesh.vtable = mesh_vtable.data();
  mesh.view_tag = 0x40;
  mesh.pass = 0x44;
  foreign = {};
  foreign.vtable = foreign_vtable.data();
  const auto reset = [&](uint32_t mode, uint32_t chunk) {
    unit = {};
    unit.group = static_cast<uint32_t>(dr::Group::COLOUR_A);
    unit.mode = mode;
    unit.chunk = chunk;
    unit.ops_begin = unit.ops_cursor = buffer.data();
    unit.ops_end = buffer.data() + buffer.size();
    buffer.fill(0);
  };
  const auto setup = [&](SynthMesh* target) {
    StackImage image = MakeImage(dr::Site::COLOUR_SETUP, 0);
    image.frame.ebx = reinterpret_cast<uint32_t>(target);
    dr::Record(&unit, &image.frame);
    return image.frame.eax;
  };
  const auto constant = [&](dr::Site site, uint32_t ecx, uint32_t state, uint32_t bits) {
    StackImage image = MakeImage(site, ecx);
    image.frame.xmm0 = bits;
    image.stack[5] = state;
    dr::Record(&unit, &image.frame);
  };
  // The setup: a known class registers its chunk (another chunk's setup of the same mesh is a conflict), an unknown one is
  // skipped and flagged, in both modes; the compare slot and the suppressed warm-up register nothing.
  di::NextOwnerCall();
  reset(dr::MODE_RECORD, 1);
  CHECK(setup(&mesh) == 0 && unit.flags == 0 && unit.ops_cursor == unit.ops_begin);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_RECORD, 2);
  CHECK(setup(&mesh) == 0 && (unit.flags & dr::SLOT_CONFLICT) != 0);
  reset(dr::MODE_RECORD, dr::COMPARE_SLOT);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_SUPPRESS, 3);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  reset(dr::MODE_RECORD, 1);
  CHECK(setup(&foreign) == 1 && (unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  reset(dr::MODE_SUPPRESS, 1);
  CHECK(setup(&foreign) == 1 && (unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  di::NextOwnerCall();
  reset(dr::MODE_RECORD, 2);
  CHECK(setup(&mesh) == 0 && unit.flags == 0);
  // The depth group has no setup site.
  reset(dr::MODE_RECORD, 1);
  unit.group = static_cast<uint32_t>(dr::Group::DEPTH_A);
  setup(&mesh);
  CHECK((unit.flags & dr::SLOT_UNKNOWN_CLASS) != 0);

  // Constants: equal ones in a row are one op with a repeat count; another value, object or op in between starts a new one.
  reset(dr::MODE_RECORD, 1);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  CHECK(unit.ops_cursor == unit.ops_begin + 1 + di::CONST_PAYLOAD);
  CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::CONSTANT) && buffer[1] == 0x2000 + di::CONST2_BLOCK && buffer[2] == 0x3f800000 && buffer[3] == 3);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST2, 0, 0x3000, 0x7fc00000);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK(unit.ops_cursor == unit.ops_begin + 4 * (1 + di::CONST_PAYLOAD));
  const uint32_t* second = buffer.data() + (1 + di::CONST_PAYLOAD);
  CHECK(second[3] == 2 && second[1] == 0x2000 + di::CONST2_BLOCK && second[2] == 0x7fc00000);
  const uint32_t* fourth = buffer.data() + 3 * (1 + di::CONST_PAYLOAD);
  CHECK(fourth[1] == 0x4000 && fourth[2] == 0 && fourth[3] == 2);
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK(fourth[3] == 3 && unit.ops_cursor == unit.ops_begin + 4 * (1 + di::CONST_PAYLOAD));
  // An op between (a texture) ends the run.
  {
    StackImage srv = MakeImage(dr::Site::COLOUR_SRV_DIFFUSE, 0x77);
    srv.frame.ebx = 0x9000;
    dr::Record(&unit, &srv.frame);
    StackImage normal = MakeImage(dr::Site::COLOUR_SRV_NORMAL, 0x78);
    normal.frame.ebx = 0x9000;
    dr::Record(&unit, &normal.frame);
  }
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  const uint32_t* diffuse_op = buffer.data() + 4 * (1 + di::CONST_PAYLOAD);
  CHECK((diffuse_op[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::SRV) && diffuse_op[1] == 0x9000 + 0x41f4 && diffuse_op[2] == 0x77);
  const uint32_t* normal_op = diffuse_op + (1 + di::SRV_PAYLOAD);
  CHECK(normal_op[1] == 0x9000 + 0x421c && normal_op[2] == 0x78);
  const uint32_t* after = normal_op + (1 + di::SRV_PAYLOAD);
  CHECK((after[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::CONSTANT) && after[3] == 1 && fourth[3] == 3);
  // The suppressed warm-up records nothing for constants and textures.
  reset(dr::MODE_SUPPRESS, 1);
  constant(dr::Site::COLOUR_CONST2, 0, 0x2000, 0x3f800000);
  CHECK(unit.ops_cursor == unit.ops_begin);

  // The faction block: the mask's index goes into the tracker's +0x0c in both modes; the op carries faction, mask, node, state.
  static SynthItem item;
  static SynthMask mask;
  mask = {};
  mask.index = 7;
  for (const uint32_t mode : {dr::MODE_RECORD, dr::MODE_SUPPRESS}) {
    for (const bool masked : {true, false}) {
      reset(mode, 1);
      item = {};
      item.mask = (masked ? &mask : nullptr);
      StackImage image = MakeImage(dr::Site::COLOUR_FACTION, 3);
      image.frame.edi = reinterpret_cast<uint32_t>(&item);
      image.stack[0x28 / 4] = 0xaaaa;
      image.stack[0x14 / 4] = 0xbbbb;
      dr::Record(&unit, &image.frame);
      int32_t stored = 0;
      std::memcpy(&stored, unit.tracker.data() + 0x0c, 4);
      CHECK(stored == (masked ? 7 : -1));
      if (mode == dr::MODE_RECORD) {
        CHECK((buffer[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::FACTION) && buffer[1] == 3 && buffer[2] == reinterpret_cast<uint32_t>(item.mask)
              && buffer[3] == 0xaaaa && buffer[4] == 0xbbbb && unit.ops_cursor == unit.ops_begin + 1 + di::FACTION_PAYLOAD);
      } else {
        CHECK(unit.ops_cursor == unit.ops_begin);
      }
    }
  }
  // A full buffer flags the chunk instead of writing past it.
  reset(dr::MODE_RECORD, 1);
  unit.ops_end = unit.ops_begin + 2;
  constant(dr::Site::COLOUR_CONST1, 0x4000, 0, 0);
  CHECK((unit.flags & dr::SLOT_OVERFLOW) != 0 && unit.ops_cursor == unit.ops_begin);
  std::printf("record (colour): setup registers / conflicts / skips an unknown class, constants merge into one op with a repeat count, textures, faction block (index stored in both modes), full buffer\n");
}

// The compare's rules for ops (S1): flush snapshots are not compared, the any byte is compared with what earlier chunks left.
static void ColourOpsEqual() {
  std::array<uint32_t, 1 + di::FLUSH_PAYLOAD> whole = {};
  std::array<uint32_t, 1 + di::FLUSH_PAYLOAD> chunk = {};
  whole[0] = chunk[0] = static_cast<uint32_t>(di::OpKind::FLUSH) | ((1 + di::FLUSH_PAYLOAD) << 16);
  for (uint32_t i = 1; i <= 10; ++i) whole[i] = chunk[i] = 0x100 + i;
  whole[1 + 13] = 1;
  chunk[1 + 13] = 0;
  CHECK(!di::OpsEqual(chunk.data(), whole.data(), 0));
  CHECK(di::OpsEqual(chunk.data(), whole.data(), 1));
  whole[1 + 11] = 0x55;
  whole[1 + 12] = 0x66;
  CHECK(di::OpsEqual(chunk.data(), whole.data(), 1));
  chunk[1 + 5] ^= 4;
  CHECK(!di::OpsEqual(chunk.data(), whole.data(), 1));
  std::array<uint32_t, 1 + di::VF68_PAYLOAD> vf_whole = {};
  std::array<uint32_t, 1 + di::VF68_PAYLOAD> vf_chunk = {};
  vf_whole[0] = vf_chunk[0] = static_cast<uint32_t>(di::OpKind::VF68) | ((1 + di::VF68_PAYLOAD) << 16);
  CHECK(di::OpsEqual(vf_chunk.data(), vf_whole.data(), 0));
  vf_whole[1 + 4] = 9;
  CHECK(!di::OpsEqual(vf_chunk.data(), vf_whole.data(), 0));
  std::printf("colour: compare rules: flush snapshots ignored, the any byte joined with the earlier chunks', vf68 snapshots compared\n");
}

// Each colour control makes one thing wrong; its run must differ.
static void ColourControls() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  const auto world = BuildWorld({.items = 6000, .seed = 71, .max_run = 8, .colour = true, .unique_meshes = true, .variation_percent = 60});
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "colour control baseline"));
  // The snapshots the ops carry (the shared-state call reads the mesh's +0x44, the flush logs it).
  di::controls.restore_snapshots = false;
  const Outcome unrestored = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.restore_snapshots = true;
  CHECK(!Equal(serial, unrestored, "control: snapshot restore off", false));
  // Seeds: the fields a chunk starts with are the warm-up's, validated at replay.
  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(serial, unvalidated, "control: colour seed validation off", false));
  // The "any" byte the loop sets itself travels in the flush op.
  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: colour any capture off", false));
  test_controls.drop_end_flush = true;
  const Outcome dropped = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  test_controls.drop_end_flush = false;
  CHECK(!Equal(serial, dropped, "control: colour dropped flush", false));
  test_controls.anim_stack = 8;
  const Outcome wrong_anim = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  test_controls.anim_stack = 9;
  CHECK(!Equal(serial, wrong_anim, "control: colour wrong anim stack slot", false));
  test_controls.inject_chunk = 2;
  const Outcome injected = RunStaged(world.get(), dr::Stage::RECORDED, 4, true);
  test_controls.inject_chunk = -1;
  CHECK(!Equal(serial, injected, "control: colour injected op", false));
  CHECK(CompareDiffers(injected));
  // A chunk that ends with the "any" byte set and nothing to flush: the compare must join it to the next chunk's first flush.
  const auto merge = BuildWorld({.items = 6000, .seed = 60, .max_run = 6, .anim_change_percent = 0, .colour = true, .unique_meshes = true, .merge = true});
  const Outcome merge_serial = RunSerial(merge.get());
  const Outcome joined = RunStaged(merge.get(), dr::Stage::RECORDED, 8, true);
  CHECK(Equal(merge_serial, joined, "pending any joined") && !CompareDiffers(joined));
  di::controls.pending_any_compare = false;
  const Outcome unjoined = RunStaged(merge.get(), dr::Stage::RECORDED, 8, true);
  di::controls.pending_any_compare = true;
  CHECK(Equal(merge_serial, unjoined, "pending any not joined: the run itself is exact"));
  CHECK(unjoined.reruns == 0 && joined.reruns == 0);
  CHECK(CompareDiffers(unjoined));
  // The repeat counts of constants: with the game's cache flag off each repeat writes and dirties, so replaying each once differs.
  const ListCase constants_list = ColourLists()[8];
  CHECK(std::string_view(constants_list.name) == "colour constants only");
  const auto constants = BuildWorld(constants_list.spec);
  cached_constants = 0;
  const Outcome constants_serial = RunSerial(constants.get());
  CHECK(Equal(constants_serial, RunStaged(constants.get(), dr::Stage::RECORDED, 8), "constants, repeats on"));
  di::controls.replay_repeats = false;
  const Outcome once = RunStaged(constants.get(), dr::Stage::RECORDED, 8);
  di::controls.replay_repeats = true;
  CHECK(!Equal(constants_serial, once, "control: constant repeat counts off", false));
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "colour controls cleared"));
  std::printf("colour controls: snapshot restore off, seed validation off, any capture off, dropped flush, wrong anim slot, injected op: each run differs (injection named by the compare); the compare without the earlier chunks' any byte reports a difference on a list whose chunks end with it set (the run itself is exact); owner table off and the continuation guard off are in their own tests\n");
#else
  std::printf("colour controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// ---------------------------------------------------------------- part 4: the B groups (C.2-5)

// A mesh of a B round: the synthetic mesh's fields, the slice at +0xac / +0xb0 and the lists at base + 4 + 32 * list type.
struct BMesh {
  SynthMesh head;
  uint8_t more[0x400 - sizeof(SynthMesh)];
};
static_assert(sizeof(BMesh) == 0x400);

struct BView {
  uint32_t* vtable;
  uint32_t type;
};

// The 23 routed classes, from the listing: the vtable, the family, the census flags of Depth B / Colour B, the list's offset and
// whether the class's virtual call at a batch start does something (the others are the stub).
struct ClassRow {
  uint32_t va;
  uint32_t family;
  bool depth;
  bool colour;
  uint32_t base;
  bool real_vf64;
};
constexpr std::array<ClassRow, 23> CLASS_ROWS = {{
    {0x11cbdb38, 0, true, true, 0x244, false},  {0x11cbdc84, 0, true, true, 0x244, false},  {0x11cbddd0, 0, false, false, 0x244, false},
    {0x11cbdf34, 1, true, true, 0x244, false},  {0x11cbe080, 1, true, true, 0x244, false},  {0x11cbf71c, 2, true, true, 0x244, false},
    {0x11cbf868, 2, true, true, 0x244, true},   {0x11cbd34c, 3, true, true, 0x244, false},  {0x11cbd498, 3, true, true, 0x244, false},
    {0x11cbd5e0, 3, true, true, 0x244, false},  {0x11cc19d0, 4, true, true, 0x10c, false},  {0x11cbd888, 5, true, true, 0x244, false},
    {0x11cbd9d4, 5, true, false, 0x244, false}, {0x11cbca34, 6, true, true, 0x244, false},  {0x11cbcb80, 6, false, false, 0x244, false},
    {0x11cbcccc, 6, true, true, 0x244, true},   {0x11cbce14, 6, true, true, 0x244, false},  {0x11cbcf5c, 6, true, true, 0x244, false},
    {0x11cbd0a4, 6, true, true, 0x244, false},  {0x11cbd1ec, 6, true, true, 0x244, false},  {0x11cbd728, 6, true, true, 0x244, false},
    {0x11cbe1e4, 6, true, true, 0x244, false},  {0x11cbf5b8, 6, true, true, 0x108, false},
}};
constexpr uint32_t CLASS_FIRST = 0x11cbca34;
// The classes the planner never records, whatever the census says (what the tests expect of the header's table): the three that
// call through runtime objects (both groups) and the colour views' own loop (Colour B).
static bool ForcedDirect(uint32_t va, bool colour) {
  return va == 0x11cbcb80 || va == 0x11cbddd0 || va == 0x11cbdc84 || (colour && va == 0x11cbd9d4);
}
constexpr uint32_t CLASS_UNKNOWN = static_cast<uint32_t>(CLASS_ROWS.size());

// The vtables sit at their listing offsets in one block (the runtime vtable minus the module shift is the listing's address, as in
// the game). Slot 0x100 holds the class's direct flags (1: Depth B, 2: Colour B) and slot 0x104 its list offset.
static std::array<uint32_t, 0x5200 / 4> vtable_pool;
static std::array<uint32_t, 0x140 / 4> unknown_vtable;
static std::array<uint32_t, 0x40 / 4> view_vtable;
static BView b_view;
static uint32_t* ClassVtable(uint32_t index) {
  return (index == CLASS_UNKNOWN ? unknown_vtable.data() : &vtable_pool[(CLASS_ROWS[index].va - CLASS_FIRST) / 4]);
}
// Violations: a direct class's body ran on a slot. What a direct class's mesh leaves in the real tracker (a test switch).
static uint32_t direct_on_slot = 0;
static uint32_t direct_leave = 0;
static uint32_t b_vf64_calls = 0;
// The depth body's variation path (a flush and the mesh's +0x44 written when the item's variation is not the tracker's; the +0x14
// reset only at a mesh setup), and a direct class's call into the dispatcher: the sorted list and range it runs, and how many
// times the classes' calls make it.
static bool depth_variation = false;
static uint32_t* nest_sorted = nullptr;
static uint32_t nest_first = 0;
static uint32_t nest_last = 0;
static uint32_t nest_budget_start = 0;
static uint32_t nest_budget = 0;

static uint8_t __thiscall FakeVf64Stub(void*, uint32_t, uint32_t, uint32_t) { return 0; }
static uint8_t __thiscall FakeVf64Real(void* mesh, uint32_t a, uint32_t b, uint32_t c) {
  const auto* m = static_cast<const SynthMesh*>(mesh);
  game_log.push_back({8, reinterpret_cast<uint32_t>(mesh), a, b, c, 0, 0, 0, 0, 0, 0, 0, m->view_tag, m->pass, 0});
  return static_cast<uint8_t>((m->kind >> 1) & 1);
}
static uint32_t __thiscall FakeViewType(void* view) { return static_cast<const BView*>(view)->type; }
static uint32_t* __thiscall FakeAccessor(void* sorted, uint32_t index) { return &static_cast<uint32_t*>(sorted)[index]; }

// The mesh's virtual call at a batch start, as a site: outside the arena the class's own function, inside a Record frame (the stub
// class records nothing).
static uint8_t Vf64Site(dr::Site site, void* t, uint32_t mesh) {
  const std::array<uint32_t, 3> arguments = {Field<uint32_t>(t, 0x1c), Field<uint32_t>(t, 0x18), Field<uint32_t>(t, 0x24)};
  if (!InArena(t)) {
    const auto function = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(
        static_cast<uintptr_t>((*reinterpret_cast<uint32_t**>(mesh))[0x64 / 4]));
    return function(reinterpret_cast<void*>(mesh), arguments[0], arguments[1], arguments[2]);
  }
  StackImage image = MakeImage(site, mesh);
  for (uint32_t i = 0; i < 3; ++i) image.stack[1 + i] = arguments[i];
  dr::Record(static_cast<dr::Slot*>(t), &image.frame);
  return static_cast<uint8_t>(image.frame.eax);
}

// A colour body's state block: the tracker writes, then the calls in the block's order (outside the arena); inside, Record.
static void StateSite(void* t, const dr::StateBlock& block) {
  if (InArena(t)) {
    StackImage image = MakeImage(dr::Site::COLOUR_B_STATE, 0);
    std::memcpy(image.stack, &block, sizeof(block));
    dr::Record(static_cast<dr::Slot*>(t), &image.frame);
    return;
  }
  if ((block.writes & dr::STATE_WRITE_FLAGS) != 0) Field<uint8_t>(t, 0x10) = static_cast<uint8_t>(block.flags);
  if ((block.writes & dr::STATE_WRITE_TEXTURES) != 0) {
    Field<uint32_t>(t, 0x04) = block.diffuse;
    Field<uint32_t>(t, 0x08) = block.normal;
  }
  const auto* mask = reinterpret_cast<const SynthMask*>(static_cast<uintptr_t>(block.mask));
  if ((block.writes & dr::STATE_WRITE_FACTION) != 0) {
    Field<int32_t>(t, 0x00) = block.faction;
    Field<int32_t>(t, 0x0c) = (mask != nullptr ? mask->index : -1);
  }
  if ((block.parts & dr::STATE_SNOW) != 0) GameConst(block.snow_object, block.snow_bits);
  if ((block.parts & dr::STATE_DIFFUSE) != 0) GameSrv(block.diffuse_slot, block.diffuse);
  if ((block.parts & dr::STATE_NORMAL) != 0) GameSrv(block.normal_slot, block.normal);
  if ((block.parts & dr::STATE_FACTION) != 0) GameFaction(block.faction, mask, reinterpret_cast<void*>(static_cast<uintptr_t>(block.node)), block.state);
}

// The depth body of a B mesh: the depth loop's state machine over the mesh's slice, without the animation, with the flush through
// the entry hook (the tests' sites ids stay for the controls), the class's virtual call at a batch start, and a flag byte (+0x12)
// that an enabled mesh with its flag set sets from the mesh (the others carry the last one's) and the flush passes on: state a
// chunk's warm-up has to reproduce.
static void __thiscall SynthBDepth(void* mesh_pointer, void* t, void* list_pointer, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  auto* mesh = static_cast<SynthMesh*>(mesh_pointer);
  const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
  for (uint32_t i = begin; i < end; ++i) {
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::DEPTH_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count, Field<uint8_t>(t, 0x12));
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    if (changed || !depth_variation) Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t kind = (mesh->kind ^ (mesh->view_tag & 0x10000)) & 0xffff;
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          if (mesh->flag != 0) Field<uint8_t>(t, 0x12) = static_cast<uint8_t>(mesh->kind & 3);
          Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::DEPTH_B_VF68, t, mesh_address, Field<uint32_t>(t, 0x1c));
          if (kind == 3) Field<uint8_t>(t, 0x69) = 1;
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    if (Field<uint32_t>(t, 0x74) == 0) continue;
    if (depth_variation && list->data[i]->variation != Field<uint32_t>(t, 0x14)) {
      FlushSite(dr::Site::DEPTH_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), Field<uint32_t>(t, 0x58), Field<uint8_t>(t, 0x12));
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
      Field<uint32_t>(t, 0x14) = list->data[i]->variation;
      mesh->pass = list->data[i]->variation;
    }
    if (Field<uint32_t>(t, 0x58) == 0) Field<uint8_t>(t, 0x69) |= Vf64Site(dr::Site::DEPTH_B_VF64, t, mesh_address);
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::DEPTH_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending, Field<uint8_t>(t, 0x12));
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

// The colour body of a B mesh: the colour loop's machine (the variation path, the state block in place of the constants,
// textures and faction block, the class's virtual call at a batch start), without the animation and the mesh setup's vtable check.
static void __thiscall SynthBColour(void* mesh_pointer, void* t, void* list_pointer, void* node_pointer, uint32_t begin, uint32_t end) {
  const auto* list = static_cast<const SynthList*>(list_pointer);
  auto* mesh = static_cast<SynthMesh*>(mesh_pointer);
  auto* node = static_cast<SynthNode*>(node_pointer);
  const uint32_t mesh_address = reinterpret_cast<uint32_t>(mesh);
  const uint32_t state = reinterpret_cast<uint32_t>(node->state);
  for (uint32_t i = begin; i < end; ++i) {
    SynthItem* item = list->data[i];
    Field<uint32_t>(t, 0x6c) = mesh_address;
    const bool changed = (mesh_address != Field<uint32_t>(t, 0x70) || Field<uint32_t>(t, 0x14) != 0);
    const uint32_t count = Field<uint32_t>(t, 0x58);
    if (changed ? count != 0 : count == Field<uint32_t>(t, 0x64)) {
      FlushSite(dr::Site::COLOUR_FLUSH_BOUNDARY, t, Field<uint32_t>(t, 0x70), Field<uint32_t>(t, 0x74), count);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
    }
    Field<uint32_t>(t, 0x70) = mesh_address;
    Field<uint32_t>(t, 0x14) = 0;
    if (changed) {
      mesh->view_tag = Field<uint32_t>(t, 0x20);
      Field<uint8_t>(t, 0x68) = mesh->flag;
      const uint32_t kind = (mesh->kind ^ (mesh->view_tag & 0x10000)) & 0xffff;
      mesh->pass = Field<uint32_t>(t, 0x14);
      const uint32_t technique = (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10));
      Field<uint32_t>(t, 0x74) = technique;
      if (technique != 0) {
        if ((technique & 1) == 0) {
          Field<uint32_t>(t, 0x50) = 4 + mesh->kind;
          Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_B_VF68, t, mesh_address, Field<uint32_t>(t, 0x1c));
        } else {
          Field<uint32_t>(t, 0x74) = 0;
        }
      }
    }
    const uint32_t technique = Field<uint32_t>(t, 0x74);
    if (technique == 0) continue;
    const uint32_t pending = Field<uint32_t>(t, 0x58);
    if (pending == Field<uint32_t>(t, 0x64) || item->variation != Field<uint32_t>(t, 0x14)) {
      FlushSite(dr::Site::COLOUR_FLUSH_VARIATION, t, Field<uint32_t>(t, 0x70), technique, pending);
      Field<uint32_t>(t, 0x58) = 0;
      Field<uint32_t>(t, 0x78) = 0;
      if (item->variation != Field<uint32_t>(t, 0x14)) {
        Field<uint32_t>(t, 0x14) = item->variation;
        mesh->pass = item->variation;
        Field<uint8_t>(t, 0x69) |= Vf68Site(dr::Site::COLOUR_B_VF68, t, mesh_address, Field<uint32_t>(t, 0x1c));
      }
    }
    // The state block: only what the tracker's fields say changed (the first constant is the "snow" one, the second sets +0x10).
    dr::StateBlock block = {};
    if ((item->flags & ITEM_CONST1) != 0) {
      block.parts |= dr::STATE_SNOW;
      block.snow_object = reinterpret_cast<uint32_t>(&const1_blocks[mesh->kind & 3]);
    }
    if ((item->flags & ITEM_CONST2) != 0) {
      block.writes |= dr::STATE_WRITE_FLAGS;
      block.flags = item->value & 0xff;
    }
    if ((item->flags & ITEM_TEXTURES) != 0 && (item->texture1 != Field<uint32_t>(t, 0x04) || item->texture2 != Field<uint32_t>(t, 0x08))) {
      block.parts |= dr::STATE_DIFFUSE | dr::STATE_NORMAL;
      block.writes |= dr::STATE_WRITE_TEXTURES;
      block.diffuse_slot = mesh_address + 0x41f4;
      block.diffuse = item->texture1;
      block.normal_slot = mesh_address + 0x421c;
      block.normal = item->texture2;
    }
    const int32_t mask_index = (item->mask != nullptr ? item->mask->index : -1);
    if ((item->flags & ITEM_FACTION) != 0 && (item->faction != Field<int32_t>(t, 0x00) || mask_index != Field<int32_t>(t, 0x0c))) {
      block.parts |= dr::STATE_FACTION;
      block.writes |= dr::STATE_WRITE_FACTION;
      block.faction = item->faction;
      block.mask = reinterpret_cast<uint32_t>(item->mask);
      block.node = reinterpret_cast<uint32_t>(node);
      block.state = state;
    }
    if ((block.parts | block.writes) != 0) StateSite(t, block);
    if (Field<uint32_t>(t, 0x58) == 0 && Field<uint8_t>(t, 0x68) != 0) {
      Field<uint8_t>(t, 0x69) |= (mesh->kind >> 1) & 1;
      Field<uint8_t>(t, 0x69) |= Vf64Site(dr::Site::COLOUR_B_VF64, t, mesh_address);
    }
    ++Field<uint32_t>(t, 0x58);
  }
  const uint32_t pending = Field<uint32_t>(t, 0x58);
  if (pending != 0) {
    FlushSite(dr::Site::COLOUR_FLUSH_END, t, Field<uint32_t>(t, 0x6c), Field<uint32_t>(t, 0x74), pending);
    Field<uint32_t>(t, 0x58) = 0;
    Field<uint32_t>(t, 0x78) = 0;
  }
}

// The mesh's slot-0x138 call (the game's per-mesh dispatcher): the view's type picks the body, the list is at the class's offset.
static void __thiscall FakeVf138(void* mesh_pointer, void* t, void* device, void* view, uint32_t list_type, void* node) {
  const uint32_t type = static_cast<const BView*>(view)->type;
  if (type == 0x49) return;
  const bool depth = (type == 0 || type == 0x55 || type == 0x60);
  const uint32_t* vtable = *reinterpret_cast<uint32_t* const*>(mesh_pointer);
  const bool direct_class = (vtable[0x100 / 4] & (depth ? 1u : 2u)) != 0;
  if (direct_class && InArena(t)) ++direct_on_slot;
  const uint32_t begin = Field<uint32_t>(mesh_pointer, 0xac);
  const uint32_t end = begin + Field<uint32_t>(mesh_pointer, 0xb0);
  void* list = static_cast<uint8_t*>(mesh_pointer) + vtable[0x104 / 4] + 32 * list_type + 4;
  if (depth) {
    SynthBDepth(mesh_pointer, t, list, begin, end);
  } else {
    SynthBColour(mesh_pointer, t, list, node, begin, end);
  }
  if (direct_class && !InArena(t)) {
    // What a class with its own body can leave behind it: a pending count, a change mask, another batch size.
    if (direct_leave == 1) Field<uint32_t>(t, 0x58) = 1;
    if (direct_leave == 2) Field<uint32_t>(t, 0x78) = 8;
    if (direct_leave == 3) Field<uint32_t>(t, 0x64) = 5;
    if (direct_leave == 4) {
      // Another view's inputs: the colour tracker's +0x20 is always the function of +0x1c that the flush writes into the mesh.
      Field<uint32_t>(t, 0x1c) ^= 0x100;
      if (!depth) Field<uint32_t>(t, 0x20) = Field<uint32_t>(t, 0x1c) ^ 0x77;
    }
    if (nest_budget > 0) {
      --nest_budget;
      dr::DispatchMesh(t, device, view, list_type, node, nest_sorted, nest_first, nest_last);
    }
  }
}

static void BSetup() {
  const auto fill = [](uint32_t* vtable, bool real_vf64, uint32_t flags, uint32_t base) {
    vtable[0x64 / 4] = reinterpret_cast<uint32_t>(real_vf64 ? &FakeVf64Real : &FakeVf64Stub);
    vtable[0x68 / 4] = reinterpret_cast<uint32_t>(&FakeVf68);
    vtable[0x100 / 4] = flags;
    vtable[0x104 / 4] = base;
    vtable[0x138 / 4] = reinterpret_cast<uint32_t>(&FakeVf138);
  };
  for (uint32_t i = 0; i < CLASS_ROWS.size(); ++i) {
    const ClassRow& row = CLASS_ROWS[i];
    fill(ClassVtable(i), row.real_vf64, (ForcedDirect(row.va, false) || !row.depth ? 1u : 0u) | (ForcedDirect(row.va, true) || !row.colour ? 2u : 0u), row.base);
  }
  fill(unknown_vtable.data(), false, 3, 0x244);
  view_vtable[0x3c / 4] = reinterpret_cast<uint32_t>(&FakeViewType);
  b_view.vtable = view_vtable.data();
}

// The B groups' part of the game: the accessor, the stub, the module shift and the class table (what the header's planner reads).
static void BGame(dr::GameCalls* game) {
  game->accessor = reinterpret_cast<uint32_t>(&FakeAccessor);
  game->vf64_stub = reinterpret_cast<uint32_t>(&FakeVf64Stub);
  game->module_shift = reinterpret_cast<uint32_t>(vtable_pool.data()) - CLASS_FIRST;
  game->class_count = static_cast<uint32_t>(CLASS_ROWS.size());
  for (uint32_t i = 0; i < CLASS_ROWS.size(); ++i) {
    const ClassRow& row = CLASS_ROWS[i];
    game->classes[i] = {.vtable = reinterpret_cast<uint32_t>(ClassVtable(i)),
                        .base = row.base,
                        .depth_body = reinterpret_cast<uint32_t>(&SynthBDepth),
                        .colour_body = reinterpret_cast<uint32_t>(&SynthBColour),
                        .family = row.family,
                        .depth = row.depth,
                        .colour = row.colour};
  }
}

struct BSpec {
  uint32_t meshes = 1500;
  uint32_t seed = 1;
  uint32_t max_items = 6;
  uint32_t none_percent = 10;
  uint32_t empty_percent = 5;
  // Meshes of a class that never records (one of the three forced classes or one outside the table).
  uint32_t direct_percent = 0;
  // The random classes include the forced ones (the planner makes them direct).
  bool all_classes = false;
  // The last meshes have no technique.
  uint32_t tail_disabled_percent = 0;
  uint32_t max_count = 1000;
  uint32_t round_numerator = 2400;
  bool colour = false;
  // The view's type (~0: the group's usual one).
  uint32_t view_type = ~0u;
  // Entries of the sorted list that repeat another's mesh.
  uint32_t duplicates = 0;
  // Every round but the last ends inside its last mesh (half of its items) and the next starts at that mesh with the rest, as the
  // game's driver does when its batch runs out (the first mesh of a round is then a straddler).
  bool restart = false;
  // The depth body's variation path (items get variations: 0 half of the time), and only the two classes with a real virtual
  // call at a batch start.
  bool depth_variation = false;
  bool real_vf64_only = false;
  // The mesh with this index is of a class that never records (~0: none).
  uint32_t direct_mesh = ~0u;
};

struct BWorld {
  std::vector<BMesh> meshes;
  std::vector<SynthItem> items;
  std::vector<SynthItem*> pointers;
  std::array<SynthMask, 3> masks = {};
  std::vector<uint32_t> sorted;
  BView view = {};
  uint32_t list_type = 1;
  bool colour = false;
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  uint32_t max_count = 1000;
  uint32_t round_numerator = 2400;
  // The tracker's state when a round starts (a round the bodies ended leaves both 0).
  uint32_t pending = 0;
  bool restart = false;
  bool depth_variation = false;
};

static std::unique_ptr<BWorld> BuildBWorld(const BSpec& spec) {
  auto world = std::make_unique<BWorld>();
  Rng random{spec.seed * 0x9e3779b97f4a7c15ull + 1};
  world->colour = spec.colour;
  world->restart = spec.restart;
  world->depth_variation = spec.depth_variation;
  world->max_count = spec.max_count;
  world->round_numerator = spec.round_numerator;
  world->view = {view_vtable.data(), (spec.view_type != ~0u ? spec.view_type : (spec.colour ? 0x10u : 0u))};
  world->meshes.resize(spec.meshes);
  // The classes meshes get: the table's rows (without the forced ones unless asked), or one that never records.
  std::vector<uint32_t> pool;
  for (uint32_t r = 0; r < CLASS_ROWS.size(); ++r) {
    if ((spec.all_classes || !(ForcedDirect(CLASS_ROWS[r].va, false) || ForcedDirect(CLASS_ROWS[r].va, true) || !CLASS_ROWS[r].depth || !CLASS_ROWS[r].colour))
        && (!spec.real_vf64_only || CLASS_ROWS[r].real_vf64)) {
      pool.push_back(r);
    }
  }
  const std::array<uint32_t, 4> direct_pool = {2, 14, 1, CLASS_UNKNOWN};
  uint32_t total = 0;
  std::vector<uint32_t> counts(spec.meshes);
  for (uint32_t m = 0; m < spec.meshes; ++m) {
    BMesh& mesh = world->meshes[m];
    uint32_t index = (random(100) < spec.direct_percent ? direct_pool[random(4)] : pool[random(static_cast<uint32_t>(pool.size()))]);
    if (m == spec.direct_mesh) index = 2;
    mesh.head.vtable = ClassVtable(index);
    mesh.head.flag = static_cast<uint8_t>(m & 1);
    const bool tail = (static_cast<uint64_t>(m) * 100 >= static_cast<uint64_t>(spec.meshes) * (100 - spec.tail_disabled_percent));
    mesh.head.kind = ((tail || random(100) < spec.none_percent) ? random(2) : 2 + random(4));
    counts[m] = (random(100) < spec.empty_percent ? 0 : 1 + random(spec.max_items));
    total += counts[m];
  }
  world->items.resize(total);
  uint32_t first = 0;
  for (uint32_t m = 0; m < spec.meshes; ++m) {
    for (uint32_t r = 0; r < counts[m]; ++r) {
      world->items[first + r].mesh = &world->meshes[m].head;
      world->items[first + r].anim = -1;
    }
    Field<uint32_t>(&world->meshes[m], 0xac) = first;
    Field<uint32_t>(&world->meshes[m], 0xb0) = counts[m];
    first += counts[m];
  }
  if (spec.colour) {
    const Spec colour_spec = {};
    FillColourFields(&world->items, &world->masks, colour_spec, &random);
  } else if (spec.depth_variation) {
    for (SynthItem& item : world->items) item.variation = (random(2) == 0 ? 0 : 1 + random(3));
  }
  world->pointers.resize(total);
  for (uint32_t k = 0; k < total; ++k) world->pointers[k] = &world->items[k];
  for (BMesh& mesh : world->meshes) {
    const uint32_t base = mesh.head.vtable[0x104 / 4];
    for (uint32_t list_type = 0; list_type < 4; ++list_type) {
      Field<SynthList>(&mesh, base + 4 + 32 * list_type) = {.count = total, .data = world->pointers.data()};
    }
    world->sorted.push_back(reinterpret_cast<uint32_t>(&mesh));
  }
  for (uint32_t d = 0; d < spec.duplicates; ++d) {
    world->sorted[(d * 37 + 5) % spec.meshes] = world->sorted[(d * 37 + 8) % spec.meshes];
  }
  return world;
}

static void ResetBWorld(BWorld* world) {
  for (BMesh& mesh : world->meshes) {
    mesh.head.view_tag = 0xffffffff;
    mesh.head.pass = 0xffffffff;
  }
  world->tracker = {};
  uint8_t* t = world->tracker.data();
  Field<uint32_t>(t, 0x18) = 0xd0;
  Field<uint32_t>(t, 0x1c) = 0xe0;
  Field<uint32_t>(t, 0x20) = 0x12345;
  Field<uint32_t>(t, 0x24) = 0xb0;
  Field<uint32_t>(t, 0x50) = 6;
  Field<uint32_t>(t, 0x64) = world->max_count;
  strict_flush = !world->colour;
  if (world->colour) {
    Field<int32_t>(t, 0x00) = -2;
    Field<int32_t>(t, 0x0c) = -2;
    Field<uint32_t>(t, 0x20) = Field<uint32_t>(t, 0x1c) ^ 0x77;
    const1_cells.fill(1.0f);
    const2_cell = 0.5f;
  }
  if (world->pending != 0) {
    BMesh& mesh = world->meshes.back();
    Field<uint32_t>(t, 0x70) = reinterpret_cast<uint32_t>(&mesh);
    Field<uint32_t>(t, 0x74) = 0x20;
    Field<uint32_t>(t, 0x58) = world->pending;
    mesh.head.view_tag = 0xabc;
    mesh.head.pass = 0x77;
  }
  g_shared->seed_anim = -1;
  g_shared->last_anim = -1;
  game_log.clear();
  direct_on_slot = 0;
  depth_variation = world->depth_variation;
  nest_budget = nest_budget_start;
}

static Outcome CaptureB(BWorld* world) {
  Outcome outcome = {.log = game_log, .tracker = world->tracker};
  for (const BMesh& mesh : world->meshes) outcome.mesh_fields.push_back({mesh.head.view_tag, mesh.head.pass});
  for (uint32_t i = 0; i < 4; ++i) std::memcpy(&outcome.cells[i], &const1_cells[i], 4);
  std::memcpy(&outcome.cells[4], &const2_cell, 4);
  return outcome;
}

static void* const B_DEVICE = reinterpret_cast<void*>(0x2000);

// The driver of the B rounds: runs of meshes [first, last] sized from the tracker's +0x50 (read after each round).
template <class RunRound>
static void BDriver(BWorld* world, RunRound&& run_round) {
  const uint32_t total = static_cast<uint32_t>(world->sorted.size());
  // The slices a restart changed (mesh index, +0xac, +0xb0 before), put back at the end.
  std::vector<std::array<uint32_t, 3>> slices;
  for (uint32_t first = 0; first < total;) {
    const uint32_t stride = std::max<uint32_t>(1, Field<uint32_t>(world->tracker.data(), 0x50));
    const uint32_t last = std::min(total, first + std::max<uint32_t>(1, world->round_numerator / stride)) - 1;
    auto* const end_mesh = reinterpret_cast<uint8_t*>(world->sorted[last]);
    const uint32_t begin = Field<uint32_t>(end_mesh, 0xac);
    const uint32_t count = Field<uint32_t>(end_mesh, 0xb0);
    const bool split = (world->restart && last + 1 < total && count >= 2);
    if (split) {
      slices.push_back({last, begin, count});
      Field<uint32_t>(end_mesh, 0xb0) = count / 2;
    }
    run_round(first, last);
    if (split) {
      Field<uint32_t>(end_mesh, 0xac) = begin + count / 2;
      Field<uint32_t>(end_mesh, 0xb0) = count - count / 2;
    }
    first = (split ? last : last + 1);
  }
  for (auto it = slices.rbegin(); it != slices.rend(); ++it) {
    auto* const mesh = reinterpret_cast<uint8_t*>(world->sorted[(*it)[0]]);
    Field<uint32_t>(mesh, 0xac) = (*it)[1];
    Field<uint32_t>(mesh, 0xb0) = (*it)[2];
  }
}

static Outcome RunBSerial(BWorld* world) {
  ResetBWorld(world);
  for (int pass = 0; pass < 2; ++pass) {
    BDriver(world, [&](uint32_t first, uint32_t last) {
      for (uint32_t i = first; i <= last; ++i) {
        const uint32_t mesh = world->sorted[i];
        reinterpret_cast<void(__thiscall*)(void*, void*, void*, void*, uint32_t, void*)>((*reinterpret_cast<uint32_t**>(mesh))[0x138 / 4])(
            reinterpret_cast<void*>(mesh), world->tracker.data(), B_DEVICE, &world->view, world->list_type, &synth_node);
      }
    });
  }
  return CaptureB(world);
}

static Outcome RunBCalls(BWorld* world, bool tracing) {
  Outcome total = {};
  for (int pass = 0; pass < 2; ++pass) {
    BDriver(world, [&](uint32_t first, uint32_t last) {
      dr::DispatchMesh(world->tracker.data(), B_DEVICE, &world->view, world->list_type, &synth_node, world->sorted.data(), first, last);
      ++total.portions;
      total.chunks += di::last_call.chunks;
      total.reruns += di::last_call.reruns;
      total.replayed += di::last_call.replayed;
      total.ops += di::last_call.ops;
      total.direct_calls += (di::last_call.direct ? 1 : 0);
      total.direct_segments += di::last_call.direct_segments;
      total.entry_reruns += di::last_call.entry_reruns;
      total.unknown_seeds += di::last_call.unknown_seeds;
      if (tracing && !di::trace.calls.empty()) {
        const di::CallTrace& call = di::trace.calls.back();
        ++total.compares[static_cast<size_t>(call.compare_result)];
        if (total.first_reason.empty()) total.first_reason = call.direct_reason;
        total.duplicates += call.duplicate_meshes;
        total.census_items += call.census_items;
        total.direct_items += call.direct_items;
        total.seed_misses += call.seed_misses;
        for (size_t kind = 0; kind < call.ops.size(); ++kind) total.kind_ops[kind] += call.ops[kind];
      }
    });
  }
  if (tracing) {
    for (const di::CallTrace& call : di::trace.calls) {
      const uint64_t trace_only = call.compare + call.run_scan + (call.stage == dr::Stage::CHUNKED ? call.warm_ups : 0);
      total.accounted = total.accounted && call.total + trace_only == call.wall;
    }
  }
  di::trace.active = false;
  Outcome outcome = CaptureB(world);
  outcome.chunks = total.chunks;
  outcome.reruns = total.reruns;
  outcome.replayed = total.replayed;
  outcome.ops = total.ops;
  outcome.direct_calls = total.direct_calls;
  outcome.portions = total.portions;
  outcome.compares = total.compares;
  outcome.kind_ops = total.kind_ops;
  outcome.direct_segments = total.direct_segments;
  outcome.entry_reruns = total.entry_reruns;
  outcome.unknown_seeds = total.unknown_seeds;
  outcome.duplicates = total.duplicates;
  outcome.census_items = total.census_items;
  outcome.direct_items = total.direct_items;
  outcome.seed_misses = total.seed_misses;
  outcome.accounted = total.accounted;
  outcome.first_reason = total.first_reason;
  return outcome;
}

// The B groups' loops bits (Depth B 4, Colour B 8).
static uint32_t b_families = 0xff;
static Outcome RunBStaged(BWorld* world, dr::Stage stage, uint32_t threads, bool tracing = false, bool update = true) {
  ResetBWorld(world);
  if (update) UpdateAt(threads, {.stage = stage, .loops = 12, .b_families = b_families});
  di::trace.active = tracing;
  di::trace.frames_left = 1u << 30;
  di::trace.calls.clear();
  return RunBCalls(world, tracing);
}

static std::vector<std::pair<const char*, BSpec>> BLists(bool colour) {
  std::vector<std::pair<const char*, BSpec>> lists = {
      {"census meshes only", {.seed = 1}},
      {"direct meshes between chunks", {.seed = 2, .direct_percent = 6}},
      {"many direct meshes", {.seed = 3, .direct_percent = 30}},
      {"empty slices", {.seed = 4, .empty_percent = 30}},
      {"long meshes, small batches", {.seed = 5, .max_items = 40, .max_count = 6}},
      {"every class", {.seed = 6, .direct_percent = 5, .all_classes = true}},
      {"last meshes without a technique", {.seed = 7, .tail_disabled_percent = 25}},
      {"mostly no technique", {.seed = 8, .none_percent = 85}},
  };
  for (auto& [name, spec] : lists) spec.colour = colour;
  return lists;
}

// The planner's class rules: 20 / 3 census / direct classes in Depth B and 19 / 4 in Colour B, and a family bit clear makes its
// classes direct.
static void BClassTable() {
  // The B bodies flush through the entry hook only.
  test_controls.entry_site = true;
  for (const bool colour : {false, true}) {
    uint32_t census = 0;
    for (uint32_t i = 0; i < CLASS_ROWS.size(); ++i) census += (di::ClassRecords(i, colour, 0xff) ? 1 : 0);
    CHECK(census == (colour ? 19u : 20u));
    CHECK(!di::ClassRecords(CLASS_UNKNOWN, colour, 0xff));
    for (uint32_t family = 0; family < 7; ++family) {
      uint32_t lost = 0;
      for (uint32_t i = 0; i < CLASS_ROWS.size(); ++i) {
        lost += (di::ClassRecords(i, colour, 0xff) && !di::ClassRecords(i, colour, 0xff & ~(1u << family)) ? 1 : 0);
      }
      CHECK(lost > 0);
    }
  }
  di::controls.class_flags = false;
  CHECK(di::ClassRecords(2, false, 0) && di::ClassRecords(14, true, 0));
  di::controls.class_flags = true;
  std::printf("B classes: 23 routed classes: Depth B 20 census / 3 direct, Colour B 19 / 4; a clear family bit makes its classes direct; a class outside the table is direct\n");
}

// The stage x K matrix of both B groups over the lists: logs, tracker and mesh fields equal the serial run.
static void BStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  uint32_t direct_segments_seen = 0;
  uint32_t entry_reruns_seen = 0;
  uint32_t replayed_seen = 0;
  uint32_t compared_equal = 0;
  std::array<uint32_t, static_cast<size_t>(di::OpKind::COUNT)> kinds = {};
  for (const bool colour : {false, true}) {
    for (const auto& [name, spec] : BLists(colour)) {
      const auto world = BuildBWorld(spec);
      const Outcome serial = RunBSerial(world.get());
      CHECK(serial.log.size() > 100);
      for (const dr::Stage stage : RECORD_STAGES) {
        for (const uint32_t k : {2u, 3u, 4u, 6u, 8u, 16u}) {
          for (const bool tracing : {false, true}) {
            const Outcome staged = RunBStaged(world.get(), stage, k, tracing);
            char label[160];
            std::snprintf(label, sizeof(label), "B %s '%s' %s K %u trace %d", colour ? "colour" : "depth", name, StageName(stage), k, tracing ? 1 : 0);
            CHECK(Equal(serial, staged, label));
            CHECK(direct_on_slot == 0);
            if (tracing) {
              CHECK(!CompareDiffers(staged));
              CHECK(staged.accounted);
              CHECK(staged.duplicates == 0);
              compared_equal += staged.compares[static_cast<size_t>(di::CompareResult::EQUAL)];
              for (size_t kind = 0; kind < kinds.size(); ++kind) kinds[kind] += staged.kind_ops[kind];
            }
            reruns_seen += staged.reruns;
            direct_segments_seen += staged.direct_segments;
            entry_reruns_seen += staged.entry_reruns;
            replayed_seen += staged.replayed;
            ++cases;
          }
        }
      }
    }
  }
  CHECK(replayed_seen > 0 && direct_segments_seen > 0 && reruns_seen > 0 && compared_equal > 0);
  for (const di::OpKind kind : {di::OpKind::FLUSH, di::OpKind::VF68, di::OpKind::VF64, di::OpKind::STATE}) {
    CHECK(kinds[static_cast<size_t>(kind)] > 0);
  }
  CHECK(kinds[static_cast<size_t>(di::OpKind::ANIM)] == 0);
  std::printf("B stages: %u cases (2 groups x 8 lists x 4 stages x 6 K x trace off / on): logs, tracker and mesh fields equal the serial run;"
              " %u chunks replayed, %u run again (%u by the entry check), %u direct segments; %u compares equal; ops flush %u vf68 %u vf64 %u state %u\n",
              cases, replayed_seen, reruns_seen, entry_reruns_seen, direct_segments_seen, compared_equal, kinds[1], kinds[2], kinds[7], kinds[8]);
}

// The view types, the fall-backs and the families.
static void BFallbacks() {
  BSpec depth_spec = {.seed = 11, .direct_percent = 4};
  const auto depth = BuildBWorld(depth_spec);
  const Outcome depth_serial = RunBSerial(depth.get());
  // Every depth view type records; the visibility view type is direct (and does nothing here).
  for (const uint32_t type : {0u, 0x55u, 0x60u}) {
    depth->view.type = type;
    const Outcome staged = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
    CHECK(Equal(depth_serial, staged, "depth view type"));
    CHECK(staged.replayed > 0 && staged.first_reason.empty());
  }
  depth->view.type = 0x49;
  const Outcome visibility_serial = RunBSerial(depth.get());
  const Outcome visibility = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
  CHECK(Equal(visibility_serial, visibility, "visibility view"));
  CHECK(visibility.first_reason == "visibility view" && visibility.direct_calls == visibility.portions && visibility_serial.log.empty());
  depth->view.type = 0;
  // A colour view type of another number.
  BSpec colour_spec = {.seed = 12, .direct_percent = 4};
  colour_spec.colour = true;
  const auto colour = BuildBWorld(colour_spec);
  const Outcome colour_serial = RunBSerial(colour.get());
  for (const uint32_t type : {0x10u, 0x5fu, 1u}) {
    colour->view.type = type;
    const Outcome staged = RunBStaged(colour.get(), dr::Stage::RECORDED, 6, true);
    CHECK(Equal(colour_serial, staged, "colour view type"));
    CHECK(staged.replayed > 0);
  }
  colour->view.type = 0x10;
  // A round of few census items.
  BSpec small_spec = {.meshes = 90, .seed = 13};
  const auto few = BuildBWorld(small_spec);
  const Outcome small_serial = RunBSerial(few.get());
  const Outcome small_staged = RunBStaged(few.get(), dr::Stage::WORKERS, 8, true);
  CHECK(Equal(small_serial, small_staged, "small round"));
  CHECK(small_staged.first_reason == "small round" && small_staged.direct_calls == small_staged.portions);
  // The stages Direct and Off, a loop group that is off, a pool smaller than the stage needs, another thread, not usable.
  for (const dr::Stage stage : {dr::Stage::DIRECT, dr::Stage::OFF}) {
    const Outcome staged = RunBStaged(depth.get(), stage, 8);
    CHECK(Equal(depth_serial, staged, "stage direct"));
    CHECK(staged.direct_calls == staged.portions);
  }
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 8});
  const Outcome loop_off = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true, false);
  CHECK(Equal(depth_serial, loop_off, "Depth B off"));
  CHECK(loop_off.first_reason == "loop off" && loop_off.direct_calls == loop_off.portions);
  UpdateAt(1, {.stage = dr::Stage::OFF});
  RunBStaged(depth.get(), dr::Stage::WORKERS, 4);
  CHECK(wi::created == 3);
  const Outcome missing = RunBStaged(depth.get(), dr::Stage::ONE_WORKER, 1);
  CHECK(Equal(depth_serial, missing, "pool missing"));
  CHECK(missing.direct_calls == missing.portions);
  // Workers with no worker on offer (a limit of 1, a pool that could not start): every round runs directly, "pool missing".
  for (const bool failed_pool : {false, true}) {
    UpdateAt(1, {.stage = dr::Stage::OFF});
    wi::limits[static_cast<size_t>(wp::User::DRAW_RECORDING)] = (failed_pool ? wp::MAX_WORKERS + 1 : 1);
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
    wi::controls.fail_thread_creation = failed_pool;
#endif
    const Outcome empty = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
    CHECK(Equal(depth_serial, empty, "empty pool"));
    CHECK(wp::Available(wp::User::DRAW_RECORDING) == 0 && empty.direct_calls == empty.portions);
    CHECK(TracedReasons("pool missing") == empty.portions);
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
    wi::controls.fail_thread_creation = false;
#endif
    wi::limits[static_cast<size_t>(wp::User::DRAW_RECORDING)] = wp::MAX_WORKERS + 1;
  }
  UpdateAt(1, {.stage = dr::Stage::OFF});
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 12});
  ResetBWorld(depth.get());
  Outcome other_outcome;
  std::thread other([&] { other_outcome = RunBCalls(depth.get(), false); });
  other.join();
  CHECK(Equal(depth_serial, other_outcome, "other thread"));
  CHECK(other_outcome.direct_calls == other_outcome.portions);
  dr::SetGame(Game(false));
  const Outcome unusable = RunBStaged(depth.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(depth_serial, unusable, "not usable"));
  CHECK(unusable.direct_calls == unusable.portions);
  dr::SetGame(Game(true));
  // A group whose sites are not all found (usable_groups), and no site of the thunks in (arena_size 0): direct.
  dr::GameCalls partial = Game(true);
  partial.usable_groups = ~(1u << static_cast<uint32_t>(dr::Group::DEPTH_B));
  dr::SetGame(partial);
  const Outcome group_out = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
  CHECK(Equal(depth_serial, group_out, "group not usable") && group_out.first_reason == "not usable" && group_out.direct_calls == group_out.portions);
  dr::SetGame(Game(true));
  const uint32_t arena_size = g_shared->arena_size;
  g_shared->arena_size = 0;
  const Outcome sites_out = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
  g_shared->arena_size = arena_size;
  CHECK(Equal(depth_serial, sites_out, "sites out") && sites_out.first_reason == "sites out" && sites_out.direct_calls == sites_out.portions);
  // A round that starts with pending instances runs directly (entry state).
  depth->pending = 5;
  const Outcome pending_serial = RunBSerial(depth.get());
  const Outcome pending = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
  CHECK(Equal(pending_serial, pending, "pending instances"));
  CHECK(pending.first_reason == "entry state");
  depth->pending = 0;
  // A family switched off: its classes become direct segments, the picture is the same.
  uint32_t family_segments = 0;
  for (uint32_t family = 0; family < 7; ++family) {
    b_families = 0xff & ~(1u << family);
    const Outcome staged = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
    CHECK(Equal(depth_serial, staged, "family off"));
    family_segments += staged.direct_segments;
  }
  b_families = 0;
  const Outcome none = RunBStaged(depth.get(), dr::Stage::WORKERS, 8, true);
  CHECK(Equal(depth_serial, none, "no family"));
  CHECK(none.first_reason == "small round" && none.direct_calls == none.portions);
  b_families = 0xff;
  CHECK(family_segments > 0);
  // A round whose first mesh is past its last does nothing (the game's loop runs no mesh).
  ResetBWorld(depth.get());
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 12, .b_families = 0xff});
  dr::DispatchMesh(depth->tracker.data(), B_DEVICE, &depth->view, depth->list_type, &synth_node, depth->sorted.data(), 5, 3);
  CHECK(game_log.empty() && depth->tracker[0x58] == 0 && Field<uint32_t>(depth->tracker.data(), 0x70) == 0);
  std::printf("B fall-backs: depth view types 0 / 0x55 / 0x60 and colour ones record; the visibility view, a small round, Direct, Off, a loop group off, a pool too small, another thread, not usable, an entry state and no family run directly; each family off keeps the picture\n");
}

// A direct segment between census chunks that leaves the tracker where a body does not: a pending count, a change mask or
// another batch size. The check before the chunk (M2) runs it directly.
static void BEntryCheck() {
  uint32_t reruns = 0;
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 21, .max_items = 20, .direct_percent = 4, .max_count = 6};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    for (const uint32_t leave : {1u, 2u, 3u, 4u}) {
      direct_leave = leave;
      const Outcome serial = RunBSerial(world.get());
      for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
        for (const uint32_t k : {3u, 8u}) {
          const Outcome staged = RunBStaged(world.get(), stage, k, true);
          CHECK(Equal(serial, staged, "entry check"));
          CHECK(!CompareDiffers(staged));
          CHECK(staged.entry_reruns > 0);
          reruns += staged.entry_reruns;
        }
      }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
      // Without the check, the chunk after the direct segment replays from the round's start state: a pending count leaves a
      // flush the chunk does not have, another batch size flushes elsewhere (a change mask alone changes nothing visible).
      di::controls.entry_check = false;
      const Outcome unchecked = RunBStaged(world.get(), dr::Stage::RECORDED, 4);
      di::controls.entry_check = true;
      // (A change of the batch size shows in Depth B only: most Colour B chunks miss their seeds and run again anyway.)
      if (leave != 2 && leave != 4 && (leave == 1 || !colour) && Equal(serial, unchecked, "control: entry check off", false)) {
        std::printf("  entry check control did not differ: colour %d leave %u\n", colour, leave);
        CHECK(false);
      }
      // The change mask (+0x78) alone: a flush after the direct segment passes it on.
      di::controls.entry_check_mask = false;
      const Outcome mask_unchecked = RunBStaged(world.get(), dr::Stage::RECORDED, 4);
      di::controls.entry_check_mask = true;
      if (leave == 2 && !colour && Equal(serial, mask_unchecked, "control: entry check mask off", false)) {
        std::printf("  entry check mask control did not differ: leave %u\n", leave);
        CHECK(false);
      }
      // Other inputs (+0x1c, which the flushes pass on): only the inputs compare sees it.
      di::controls.entry_check_inputs = false;
      const Outcome inputs_unchecked = RunBStaged(world.get(), dr::Stage::RECORDED, 4);
      di::controls.entry_check_inputs = true;
      if (leave == 4 && !colour && Equal(serial, inputs_unchecked, "control: entry check inputs off", false)) {
        std::printf("  entry check inputs control did not differ: leave %u\n", leave);
        CHECK(false);
      }
#endif
    }
    direct_leave = 0;
  }
  std::printf("B entry check: a direct segment that leaves a count, a mask, another batch size or other inputs (+0x1c) makes the next chunk run directly (%u chunks), logs equal; with the check off (or the mask term alone) the count, batch size, mask and inputs cases differ\n", reruns);
}

// Seeds: the warm-ups, the step back over meshes without a technique, the unknown seed after four, validation.
static void BSeeds() {
  uint32_t unknown = 0;
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 31, .none_percent = 85};
    spec.colour = colour;
    const auto sparse = BuildBWorld(spec);
    const Outcome serial = RunBSerial(sparse.get());
    for (const dr::Stage stage : RECORD_STAGES) {
      const Outcome staged = RunBStaged(sparse.get(), stage, 8, true);
      CHECK(Equal(serial, staged, "no technique before chunks"));
      CHECK(!CompareDiffers(staged));
      unknown += staged.unknown_seeds;
    }
    BSpec mixed_spec = {.seed = 32, .direct_percent = 4};
    mixed_spec.colour = colour;
    const auto mixed = BuildBWorld(mixed_spec);
    const Outcome mixed_serial = RunBSerial(mixed.get());
    CHECK(Equal(mixed_serial, RunBStaged(mixed.get(), dr::Stage::WORKERS, 8, true), "direct meshes change the seed"));
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    // The direct meshes between chunks change the fields a warm-up could not see: without validation the replay starts wrong.
    di::controls.validate_seeds = false;
    const Outcome unvalidated = RunBStaged(mixed.get(), dr::Stage::RECORDED, 8);
    di::controls.validate_seeds = true;
    CHECK(!Equal(mixed_serial, unvalidated, "control: B seed validation off", false));
#endif
  }
  CHECK(unknown > 0);
  std::printf("B seeds: warm-ups step back over meshes without a technique, %u chunks had no seed after 4 meshes (run again), logs equal; validation off differs\n", unknown);
}

// The stride (+0x50): the driver sizes its next round from it; a chunk that wrote none leaves the real one.
static void BStride() {
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 41, .direct_percent = 5, .tail_disabled_percent = 30};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    const Outcome serial = RunBSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      CHECK(Equal(serial, RunBStaged(world.get(), stage, 8, true), "stride"));
    }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    di::controls.stride_non_zero = false;
    const Outcome zeroed = RunBStaged(world.get(), dr::Stage::RECORDED, 8);
    di::controls.stride_non_zero = true;
    CHECK(!Equal(serial, zeroed, "control: stride copied always", false));
#endif
  }
  std::printf("B stride: the stride is zeroed after the fill and the warm-ups and copied back only when a chunk wrote it; copying it always differs\n");
}

// Overflowing chunks run again; a mesh twice in a round (trace frames) makes the round direct and is counted.
static void BOverflowAndDuplicates() {
  BSpec spec = {.seed = 51, .direct_percent = 3};
  const auto world = BuildBWorld(spec);
  const Outcome serial = RunBSerial(world.get());
  uint32_t reruns = 0;
  for (const uint32_t limit : {600u, 3000u}) {
    di::op_buffer_limit = limit;
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      const Outcome staged = RunBStaged(world.get(), stage, 6, true);
      CHECK(Equal(serial, staged, "B tiny buffers"));
      CHECK(!CompareDiffers(staged));
      reruns += staged.reruns;
    }
  }
  di::op_buffer_limit = di::OP_ARENA_BYTES;
  CHECK(reruns > 0);
  spec.duplicates = 30;
  const auto repeating = BuildBWorld(spec);
  const Outcome repeating_serial = RunBSerial(repeating.get());
  const Outcome repeated = RunBStaged(repeating.get(), dr::Stage::WORKERS, 6, true);
  CHECK(Equal(repeating_serial, repeated, "duplicate meshes"));
  CHECK(repeated.duplicates > 0 && repeated.first_reason == "duplicate meshes");
  std::printf("B overflow and duplicates: tiny op buffers run chunks again (%u), logs equal; a mesh twice in a round runs it directly and is counted\n", reruns);
}

// The ops: a stub class's virtual call records nothing, another's is deferred; a state block's tracker writes happen in both
// modes and its calls replay in the block's order.
static void BRecordRules() {
  uint32_t used = 0;
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> real = {};
  dr::Slot* slot = di::PrepareSlot(0, static_cast<uint32_t>(dr::Group::COLOUR_B), real.data(), 4096, &used);
  BMesh stub_mesh = {};
  BMesh real_mesh = {};
  stub_mesh.head.vtable = ClassVtable(0);
  real_mesh.head.vtable = ClassVtable(6);
  real_mesh.head.view_tag = 5;
  real_mesh.head.pass = 6;
  Field<uint32_t>(slot, 0x1c) = 0x11;
  Field<uint32_t>(slot, 0x18) = 0x22;
  Field<uint32_t>(slot, 0x24) = 0x33;
  CHECK(Vf64Site(dr::Site::COLOUR_B_VF64, slot, reinterpret_cast<uint32_t>(&stub_mesh)) == 0);
  CHECK(slot->ops_cursor == slot->ops_begin);
  CHECK(Vf64Site(dr::Site::COLOUR_B_VF64, slot, reinterpret_cast<uint32_t>(&real_mesh)) == 0);
  CHECK(slot->ops_cursor == slot->ops_begin + 1 + di::VF64_PAYLOAD);
  CHECK((slot->ops_begin[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::VF64) && slot->ops_begin[2] == 0x11 && slot->ops_begin[3] == 0x22
        && slot->ops_begin[4] == 0x33 && slot->ops_begin[5] == 5 && slot->ops_begin[6] == 6);
  // A group A slot refuses a B site.
  dr::Slot* other = di::PrepareSlot(1, static_cast<uint32_t>(dr::Group::DEPTH_A), real.data(), 4096, &used);
  Vf64Site(dr::Site::COLOUR_B_VF64, other, reinterpret_cast<uint32_t>(&real_mesh));
  CHECK((other->flags & dr::SLOT_UNKNOWN_CLASS) != 0);
  // The state block: writes in both modes, the op only when recording.
  dr::StateBlock block = {.parts = dr::STATE_SNOW | dr::STATE_DIFFUSE | dr::STATE_NORMAL | dr::STATE_FACTION,
                          .writes = dr::STATE_WRITE_FLAGS | dr::STATE_WRITE_TEXTURES | dr::STATE_WRITE_FACTION,
                          .flags = 0x5a,
                          .snow_object = reinterpret_cast<uint32_t>(&const1_blocks[1]),
                          .snow_bits = 0,
                          .diffuse_slot = 0x41f4,
                          .diffuse = 3,
                          .normal_slot = 0x421c,
                          .normal = 2,
                          .faction = -1,
                          .mask = 0,
                          .node = reinterpret_cast<uint32_t>(&synth_node),
                          .state = reinterpret_cast<uint32_t>(synth_node.state)};
  uint32_t* const before = slot->ops_cursor;
  slot->mode = dr::MODE_SUPPRESS;
  StateSite(slot, block);
  CHECK(slot->ops_cursor == before && slot->tracker[0x10] == 0x5a && Field<uint32_t>(slot, 0x04) == 3 && Field<uint32_t>(slot, 0x08) == 2
        && Field<int32_t>(slot, 0x00) == -1 && Field<int32_t>(slot, 0x0c) == -1);
  slot->mode = dr::MODE_RECORD;
  Field<uint32_t>(slot, 0x04) = 0;
  StateSite(slot, block);
  CHECK(slot->ops_cursor == before + 1 + di::STATE_PAYLOAD && Field<uint32_t>(slot, 0x04) == 3);
  // The replay: the real tracker's cells and the log get the calls in the block's order.
  game_log.clear();
  const1_cells.fill(1.0f);
  cached_constants = 0;
  di::Replay replay = {.real = real.data(), .group_index = static_cast<uint32_t>(dr::Group::COLOUR_B), .last_anim = -1, .op_count = 0, .trace_call = nullptr};
  real_mesh.head.view_tag = 5;
  di::ReplayChunk(&replay, slot);
  CHECK(replay.op_count == 2);
  CHECK(game_log.size() == 5 && game_log[0][0] == 8 && game_log[1][0] == 4 && game_log[2][0] == 5 && game_log[3][0] == 5 && game_log[4][0] == 6);
  CHECK(game_log[2][1] == 0x41f4 && game_log[2][2] == FakeView(3) && game_log[3][1] == 0x421c);
  std::printf("B records: a stub class's virtual call records nothing, another's is deferred with its arguments and snapshots; a state block writes the tracker in both modes and replays its calls in order\n");
}

// The trace of the B groups: armed from the environment, the last frame compares (per run of census chunks), the file names the
// B round's numbers (the gates).
static void BTrace() {
  BSpec spec = {.seed = 81, .direct_percent = 4};
  spec.colour = true;
  const auto world = BuildBWorld(spec);
  const std::filesystem::path file = std::filesystem::path(__argv[0]).parent_path() / "tw_draw_recording.txt";
  std::filesystem::remove(file);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", "0,3");
  di::trace.read_environment = false;
  di::trace.armed = false;
  di::trace.finished = false;
  di::controls.compare_every_call = false;
  ResetBWorld(world.get());
  const dr::Settings settings = {.stage = dr::Stage::WORKERS, .loops = 12};
  UpdateAt(6, settings);
  CHECK(di::trace.armed);
  UpdateAt(6, settings);
  CHECK(di::trace.active);
  for (int frame = 0; frame < 2; ++frame) {
    const size_t calls_before = di::trace.calls.size();
    BDriver(world.get(), [&](uint32_t first, uint32_t last) {
      dr::DispatchMesh(world->tracker.data(), B_DEVICE, &world->view, world->list_type, &synth_node, world->sorted.data(), first, last);
    });
    CHECK(di::trace.calls.size() > calls_before);
    for (size_t i = calls_before; i < di::trace.calls.size(); ++i) {
      const di::CallTrace& call = di::trace.calls[i];
      CHECK(call.compared == (frame == 1));
      CHECK(call.group == dr::Group::COLOUR_B);
      CHECK((call.compare_result == di::CompareResult::NONE) == !(frame == 1 && call.chunks > 1));
      CHECK(call.meshes > 0 && call.census_items + call.direct_items > 0 && call.round_meshes == call.meshes);
      uint32_t family_items = 0;
      uint32_t family_chunks = 0;
      for (const uint32_t items : call.family_items) family_items += items;
      for (const uint32_t chunks : call.family_chunks) family_chunks += chunks;
      CHECK(family_items <= call.census_items && (call.chunks == 0 || (family_items > 0 && family_chunks >= call.chunks)));
    }
    UpdateAt(6, settings);
  }
  di::controls.compare_every_call = true;
  CHECK(!di::trace.active && di::trace.finished);
  std::ifstream stream(file, std::ios::binary);
  const std::string text_value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(text_value.find("group 3 family -") != std::string::npos);
  CHECK(text_value.find("B families (census items / chunks): f0 ") != std::string::npos && text_value.find(" f7 ") != std::string::npos);
  CHECK(text_value.find("cut: targets") == std::string::npos);
  CHECK(text_value.find("B round: meshes ") != std::string::npos && text_value.find("duplicate meshes 0") != std::string::npos);
  CHECK(text_value.find("seeds hit ") != std::string::npos && text_value.find("planner ") != std::string::npos);
  CHECK(text_value.find("vf64 ") != std::string::npos && text_value.find("state ") != std::string::npos);
  CHECK(text_value.find("compare: equal") != std::string::npos || text_value.find("compare: not comparable") != std::string::npos);
  CHECK(text_value.find("differs") == std::string::npos);
  SetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", nullptr);
  stream.close();
  std::filesystem::remove(file);
  std::printf("B trace: armed from the environment wrote the file (group, the B round's meshes, census share and per-family numbers, seeds, planner time), compare not different\n");
}

// A compare difference latches the group off (S9b): the next rounds run directly, logs equal.
static void BLatch() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  BSpec spec = {.seed = 61, .max_items = 20, .max_count = 6};
  const auto world = BuildBWorld(spec);
  const Outcome serial = RunBSerial(world.get());
  // An op injected into a chunk the compare sees (a chunk run again hides it): the first of chunks 1 to 3 that replays.
  Outcome injected;
  for (const int chunk : {1, 2, 3}) {
    di::latched_groups = 0;
    di::controls.latch_groups = true;
    test_controls.inject_chunk = chunk;
    injected = RunBStaged(world.get(), dr::Stage::RECORDED, 4, true);
    test_controls.inject_chunk = -1;
    di::controls.latch_groups = false;
    if (CompareDiffers(injected)) break;
  }
  CHECK(CompareDiffers(injected));
  CHECK((di::latched_groups & (1u << static_cast<uint32_t>(dr::Group::DEPTH_B))) != 0);
  di::controls.latch_groups = true;
  const Outcome latched = RunBStaged(world.get(), dr::Stage::RECORDED, 4, true);
  di::controls.latch_groups = false;
  CHECK(Equal(serial, latched, "latched off"));
  CHECK(latched.first_reason == "latched off" && latched.direct_calls == latched.portions);
  di::latched_groups = 0;
  std::printf("B latch: a compare that differs switches the group off for the rest of the run; the next rounds run directly\n");
#else
  std::printf("B latch: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// A round that restarts at the previous round's last mesh (first != last + 1): the mesh is continued with no setup of its own (a
// straddler). The compare runs of a trace frame set it up like any mesh; the chunk that records it must start from the state the real
// run left, not the compare run's (M-2): logs and meshes equal the serial run, the compare is equal and the group stays on.
static void BStraddler() {
  uint32_t equal_compares = 0;
  uint32_t cases = 0;
  for (const bool colour : {false, true}) {
    BSpec spec = {.meshes = 1200, .seed = 91, .max_items = 16, .none_percent = 0, .empty_percent = 0, .round_numerator = 700,
                  .restart = true, .depth_variation = !colour, .real_vf64_only = true};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    const Outcome serial = RunBSerial(world.get());
    CHECK(serial.log.size() > 100);
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    di::latched_groups = 0;
    di::controls.latch_groups = true;
#endif
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (const uint32_t k : {2u, 3u, 4u, 6u, 8u, 16u}) {
        for (const bool tracing : {false, true}) {
          const Outcome staged = RunBStaged(world.get(), stage, k, tracing);
          char label[160];
          std::snprintf(label, sizeof(label), "B straddler %s %s K %u trace %d", colour ? "colour" : "depth", StageName(stage), k, tracing ? 1 : 0);
          CHECK(Equal(serial, staged, label));
          CHECK(direct_on_slot == 0);
          CHECK(staged.replayed > 0);
          if (tracing) {
            CHECK(!CompareDiffers(staged));
            equal_compares += staged.compares[static_cast<size_t>(di::CompareResult::EQUAL)];
          }
          ++cases;
        }
      }
    }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    CHECK(di::latched_groups == 0);
    di::controls.latch_groups = false;
    // Without the restore a compared round that starts at a straddler of a real virtual call leaves the chunk the compare run's
    // state: the log or the compare differs (Depth B only: a colour body sets a mesh up at its entry).
    if (!colour) {
      di::controls.restore_straddler = false;
      bool differs = false;
      for (const uint32_t k : {2u, 4u, 8u}) {
        const Outcome unrestored = RunBStaged(world.get(), dr::Stage::RECORDED, k, true);
        differs = differs || !Equal(serial, unrestored, "control: straddler restore off", false) || CompareDiffers(unrestored);
      }
      di::controls.restore_straddler = true;
      CHECK(differs);
      // R-S1: chunk 0 of a round that starts at a straddler run again (overflow), or the whole round run directly (a damaged op
      // buffer), starts from the straddler's state the round began with: its recording wrote the mesh's +0x44 (the real virtual
      // call logs it). Without the restore the logs differ.
      for (const bool damaged : {false, true}) {
        bool restore_differs = false;
        for (const bool restore : {true, false}) {
          di::controls.restore_straddler = restore;
          test_controls.overflow_chunk = (damaged ? -1 : 0);
          test_controls.corrupt_chunk = (damaged ? 0 : -1);
          test_controls.corrupt_keep = true;
          for (const uint32_t k : {2u, 4u, 8u}) {
            const Outcome rerun = RunBStaged(world.get(), dr::Stage::RECORDED, k, false);
            CHECK(rerun.reruns > 0 || damaged);
            if (restore) {
              CHECK(Equal(serial, rerun, "R-S1 straddler chunk 0 run again"));
            } else {
              restore_differs = restore_differs || !Equal(serial, rerun, "control: R-S1 restore off", false);
            }
          }
          test_controls.overflow_chunk = -1;
          test_controls.corrupt_chunk = -1;
          test_controls.corrupt_keep = false;
          di::controls.restore_straddler = true;
        }
        CHECK(restore_differs);
      }
    }
#endif
  }
  CHECK(equal_compares > 0);
  std::printf("B straddler: %u cases (rounds restart at the previous round's last mesh, real virtual call classes, the variation path): logs and meshes equal the serial run, %u compares equal, no group latched; the restore off differs\n", cases, equal_compares);
}

// Runs of census chunks in one round (a direct mesh between them): the compare of each run is combined by severity. Run 0 run again
// (an overflow) and an op injected into a chunk of run 1: the round's compare says DIFFERS, run 1, and the group latches off.
static void BCompareRuns() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  bool found = false;
  for (const bool colour : {false, true}) {
    BSpec spec = {.meshes = 300, .seed = 95, .max_items = 20, .none_percent = 0, .empty_percent = 0, .max_count = 6, .round_numerator = 6000,
                  .direct_mesh = 150};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    const Outcome serial = RunBSerial(world.get());
    const uint32_t group_bit = 1u << static_cast<uint32_t>(colour ? dr::Group::COLOUR_B : dr::Group::DEPTH_B);
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (const uint32_t k : {4u, 8u}) {
        // Run 0 is run again and nothing else is wrong: exact, and the compare says so (not DIFFERS, run 0).
        di::latched_groups = 0;
        test_controls.overflow_chunk = 0;
        const Outcome rerun = RunBStaged(world.get(), stage, k, true);
        test_controls.overflow_chunk = -1;
        CHECK(Equal(serial, rerun, "B compare runs: run 0 run again"));
        CHECK(rerun.direct_segments > 0 && rerun.reruns > 0 && !di::trace.calls.empty());
        CHECK(di::trace.calls.front().compare_result == di::CompareResult::CHUNK_RERUN && di::trace.calls.front().compare_run == 0);
        for (uint32_t chunk = 1; chunk < k; ++chunk) {
          di::latched_groups = 0;
          di::controls.latch_groups = true;
          test_controls.overflow_chunk = 0;
          test_controls.inject_chunk = static_cast<int>(chunk);
          // The op's own mesh (any mesh of the round) names the class, not the chunk's first mesh.
          test_controls.inject_mesh = world->sorted[(chunk * 53 + k * 7) % world->sorted.size()];
          RunBStaged(world.get(), stage, k, true);
          test_controls.inject_mesh = 0;
          test_controls.overflow_chunk = -1;
          test_controls.inject_chunk = -1;
          di::controls.latch_groups = false;
          CHECK(!di::trace.calls.empty());
          const di::CallTrace& call = di::trace.calls.front();
          if (call.compare_result == di::CompareResult::DIFFERS) {
            // The op index is the run's own; the chunk is the one the op was injected into.
            CHECK(call.compare_run == 1 && call.compare_chunk == chunk);
            CHECK(call.family != di::NO_FAMILY && call.compare_class < CLASS_ROWS.size());
            // The class of the injected op's own mesh (not the chunk's first).
            CHECK(di::game_calls.classes[call.compare_class].vtable == *reinterpret_cast<const uint32_t*>(injected_mesh));
            CHECK(call.family == di::game_calls.classes[call.compare_class].family);
            CHECK((di::latched_groups & group_bit) != 0);
            found = true;
          } else {
            // Hidden: an injection into run 0 stops at its rerun; one into a chunk of run 1 that ran again too.
            CHECK(call.compare_result == di::CompareResult::CHUNK_RERUN && (di::latched_groups & group_bit) == 0);
          }
        }
      }
    }
  }
  di::latched_groups = 0;
  CHECK(found);
  std::printf("B compare runs: two runs in a round, run 0 run again and an op injected into run 1: the round's compare says DIFFERS (run 1, the chunk and the class and family of the op's mesh) and the group latches\n");
#else
  std::printf("B compare runs: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// A round that lists more meshes than the planner holds runs directly; the trace counts what it listed (N-2).
static void BOversizeRound() {
  BSpec spec = {.meshes = di::MAX_ROUND_MESHES + 300, .seed = 111, .max_items = 2, .none_percent = 0, .empty_percent = 0,
                .round_numerator = 1u << 28};
  const auto world = BuildBWorld(spec);
  const Outcome serial = RunBSerial(world.get());
  const Outcome staged = RunBStaged(world.get(), dr::Stage::RECORDED, 4, true);
  CHECK(Equal(serial, staged, "round over the mesh limit"));
  CHECK(staged.first_reason == "too many meshes" && staged.direct_calls == staged.portions && !di::trace.calls.empty());
  CHECK(di::trace.calls.front().round_meshes == di::MAX_ROUND_MESHES + 300 && di::trace.calls.front().meshes == 0);
  std::printf("B oversize round: %u meshes in one round run directly, logs equal the serial run, the trace lists them\n", di::MAX_ROUND_MESHES + 300);
}

// N-1 for the B rounds.
static void BBadOps() {
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 112, .max_items = 20, .direct_percent = 3, .max_count = 6};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    const Outcome serial = RunBSerial(world.get());
    for (const uint32_t chunk : {1u, 2u}) {
      test_controls.corrupt_chunk = static_cast<int>(chunk);
      const Outcome staged = RunBStaged(world.get(), dr::Stage::RECORDED, 4, true);
      CHECK(test_controls.corrupt_chunk == -1);
      test_controls.corrupt_chunk = -1;
      CHECK(Equal(serial, staged, "B damaged op buffer"));
      CHECK(staged.first_reason == "bad ops" && staged.replayed > 0);
    }
  }
  std::printf("B bad ops: a zero op header in one chunk makes that round run directly, logs equal the serial run\n");
}

// A dispatcher call inside a round (here a direct class's own call, made on the real tracker while the round's chunks are waiting
// to replay) runs directly: it would take the slots and the planned meshes the round is using (S-2).
static void BNested() {
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 101, .max_items = 12, .direct_percent = 3};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    nest_sorted = world->sorted.data();
    nest_first = 100;
    nest_last = 420;
    nest_budget_start = 2;
    UpdateAt(1, {.stage = dr::Stage::OFF});
    const Outcome serial = RunBSerial(world.get());
    CHECK(nest_budget == 0);
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      di::nested_calls = 0;
      const Outcome staged = RunBStaged(world.get(), stage, 8);
      CHECK(Equal(serial, staged, "nested dispatcher calls"));
      CHECK(nest_budget == 0 && direct_on_slot == 0 && staged.replayed > 0 && staged.direct_segments > 0);
      CHECK(!di::in_round);
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
      // The classes' two calls (one per budget step), each run directly as a nested call.
      CHECK(di::nested_calls == 2);
#endif
    }
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    // Without the guard the nested round plans over the outer round's meshes and takes its slots.
    di::controls.nested_guard = false;
    di::nested_calls = 0;
    const Outcome unguarded = RunBStaged(world.get(), dr::Stage::RECORDED, 8);
    di::controls.nested_guard = true;
    di::in_round = false;
    CHECK(!Equal(serial, unguarded, "control: B nesting guard off", false) && di::nested_calls == 0);
#endif
    nest_budget_start = 0;
    nest_budget = 0;
  }
  std::printf("B nested: a dispatcher call made from a direct class's own call inside a round runs directly, logs equal the serial run\n");
}

// S-1: the same in a round that is too small to record (it runs its planned meshes directly, and a direct class's call into the
// dispatcher must not plan over them).
static void BNestedSmallRound() {
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 103, .max_items = 12, .direct_percent = 3, .round_numerator = 300};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    nest_sorted = world->sorted.data();
    nest_first = 100;
    nest_last = 420;
    nest_budget_start = 2;
    UpdateAt(1, {.stage = dr::Stage::OFF});
    const Outcome serial = RunBSerial(world.get());
    CHECK(nest_budget == 0);
    di::nested_calls = 0;
    const Outcome staged = RunBStaged(world.get(), dr::Stage::RECORDED, 8, true);
    CHECK(Equal(serial, staged, "nested call in a small round"));
    CHECK(staged.first_reason == "small round" && staged.direct_calls == staged.portions && nest_budget == 0 && !di::in_round);
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
    CHECK(di::nested_calls == 2);
    di::controls.nested_guard = false;
    di::nested_calls = 0;
    const Outcome unguarded = RunBStaged(world.get(), dr::Stage::RECORDED, 8);
    di::controls.nested_guard = true;
    di::in_round = false;
    CHECK(!Equal(serial, unguarded, "control: small round nesting guard off", false) && di::nested_calls == 0);
#endif
    nest_budget_start = 0;
    nest_budget = 0;
  }
  std::printf("B nested, small round: a dispatcher call from a direct class inside a round that runs directly is nested too; the guard off differs\n");
}

// Each B control makes one thing wrong; its run must differ (or its violation must show).
static void BControls() {
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  for (const bool colour : {false, true}) {
    BSpec spec = {.seed = 71, .max_items = 20, .direct_percent = 8, .max_count = 6};
    spec.colour = colour;
    const auto world = BuildBWorld(spec);
    const Outcome serial = RunBSerial(world.get());
    CHECK(Equal(serial, RunBStaged(world.get(), dr::Stage::RECORDED, 8, true), "B control baseline"));
    CHECK(direct_on_slot == 0);
    // The census flags: a direct class's body must never run on a slot.
    di::controls.class_flags = false;
    RunBStaged(world.get(), dr::Stage::RECORDED, 8);
    di::controls.class_flags = true;
    CHECK(direct_on_slot > 0);
    di::controls.capture_any = false;
    CHECK(!Equal(serial, RunBStaged(world.get(), dr::Stage::RECORDED, 8), "control: B any capture off", false));
    di::controls.capture_any = true;
    // (Depth B's meshes keep one view tag and pass: its snapshots equal what the replay finds anyway.)
    di::controls.restore_snapshots = false;
    if (colour) CHECK(!Equal(serial, RunBStaged(world.get(), dr::Stage::RECORDED, 8), "control: B snapshot restore off", false));
    di::controls.restore_snapshots = true;
    test_controls.drop_end_flush = true;
    CHECK(!Equal(serial, RunBStaged(world.get(), dr::Stage::RECORDED, 4), "control: B dropped end flush", false));
    test_controls.drop_end_flush = false;
    // An op injected into one chunk: one that replays (a chunk run again hides it, and stops the compare after it) must differ and be named. (A list
    // without direct meshes: after those most chunks miss their seeds and run again.)
    BSpec plain_spec = {.seed = 72, .max_items = 20, .max_count = 6};
    plain_spec.colour = colour;
    const auto plain = BuildBWorld(plain_spec);
    const Outcome plain_serial = RunBSerial(plain.get());
    bool differs = false;
    bool named = false;
    for (const uint32_t k : {2u, 3u, 4u, 8u}) {
      for (uint32_t chunk = 1; chunk < k; ++chunk) {
        test_controls.inject_chunk = static_cast<int>(chunk);
        const Outcome injected = RunBStaged(plain.get(), dr::Stage::RECORDED, k, true);
        test_controls.inject_chunk = -1;
        differs = differs || !Equal(plain_serial, injected, "control: B injected op", false);
        named = named || CompareDiffers(injected);
      }
    }
    CHECK(differs && named);
    CHECK(Equal(serial, RunBStaged(world.get(), dr::Stage::WORKERS, 8), "B controls cleared"));
  }
  std::printf("B controls: class flags off (a direct class runs on a slot), any capture off, snapshot restore off, dropped end flush, injected op: each shows; the other controls are in their tests\n");
#else
  std::printf("B controls: skipped (TWATTILA_DRAW_RECORDING_CONTROLS not defined)\n");
#endif
}

// The groups the engine code keeps its sites in: none while the stage is Off; with the stage on, a group stays once on until
// the stage goes Off (the live mask is Settings::loops).
static void SiteGroupsSticky() {
  CHECK(dr::SiteGroups(dr::Stage::OFF, 15) == 0);
  CHECK(dr::SiteGroups(dr::Stage::OFF, 0) == 0);
  CHECK(dr::SiteGroups(dr::Stage::WORKERS, 1) == 1);
  CHECK(dr::SiteGroups(dr::Stage::WORKERS, 4) == 5);
  CHECK(dr::SiteGroups(dr::Stage::WORKERS, 0) == 5);
  CHECK(dr::SiteGroups(dr::Stage::DIRECT, 8) == 13);
  CHECK(dr::SiteGroups(dr::Stage::OFF, 15) == 0);
  CHECK(dr::SiteGroups(dr::Stage::RECORDED, 2) == 2);
  CHECK(dr::SiteGroups(dr::Stage::OFF, 0) == 0);
  std::printf("site groups: none while the stage is Off; sticky while it is on\n");
}

static void Detach() {
  const auto world = BuildWorld({.items = 6000, .seed = 31, .max_run = 10});
  const Outcome serial = RunSerial(world.get());
  // The pool starts from nothing (earlier tests grew it to 15).
  UpdateAt(1, {.stage = dr::Stage::OFF});
  UpdateAt(8, {.stage = dr::Stage::WORKERS, .loops = 1});
  CHECK(wi::created == 7);
  // The add-on's order: the pool stops first, then the users' Uninstall. Sites still in: everything stays, the dispatcher runs
  // directly.
  CHECK(g_shared->arena_size != 0);
  wp::Stop();
  CHECK(wi::created == 0);
  dr::Uninstall(true);
  CHECK(di::detached.load() && di::shared != nullptr);
  // S-2: a kept detach leaves the thunks as they are (a dispatch may still be recording).
  CHECK(g_shared->arena_size != 0);
  const Outcome detached = RunStaged(world.get(), dr::Stage::WORKERS, 8);
  CHECK(Equal(serial, detached, "detached"));
  CHECK(detached.direct_calls == detached.portions);
  // A dispatch still counted active: kept too.
  g_shared->active = 1;
  wp::Stop();
  dr::Uninstall(false);
  CHECK(di::shared != nullptr);
  g_shared->active = 0;
  wp::Stop();
  dr::Uninstall(false);
  CHECK(di::shared == nullptr && di::arena == nullptr && di::module_reference == nullptr && wi::created == 0);
  std::printf("detach: kept while sites are in or a dispatch is active; clean case frees the memory (the pool stopped first)\n");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  PoolStartStop();
  PoolLiveK();
  PoolControlWords();
  PoolCpuSets();
  PoolThreadFailure();
  PoolAllClaims();
  PoolLateWorker();
  PoolMidJobWait();
  PoolRefusals();
  PoolRefusedAllCompletes();
  PoolStopWithOpenJob();
  PoolStopHoldsBusy();
  PoolEnteredStress();
  PoolSlowWork();
  PoolOpenAtUpdate();
  PoolEnvironment();
  PoolArmTrace();
  Setup();
  RecordRules();
  DepthStages();
  DepthFlushEntry();
  DepthTouchAhead();
  DepthOverflow();
  DepthLarge();
  DepthEntryState();
  DepthContinuation();
  DepthFallbacks();
  DepthTrace();
  DepthControls();
  DepthNested();
  DepthBadOps();
  AnyJoinControl();
  ColourRecordRules();
  ColourOpsEqual();
  ColourReplayUnits();
  ColourStages();
  ColourInterleaved();
  ColourUnknownClass();
  ColourContinuation();
  ColourForeignContinuation();
  ColourClaimControl();
  ColourWarmUpRestore();
  ColourControls();
  BClassTable();
  BStages();
  BFallbacks();
  BEntryCheck();
  BSeeds();
  BStride();
  BOverflowAndDuplicates();
  BRecordRules();
  BTrace();
  BLatch();
  BStraddler();
  BCompareRuns();
  BOversizeRound();
  BBadOps();
  BNested();
  BNestedSmallRound();
  BControls();
  SiteGroupsSticky();
  test_controls.entry_site = false;
  Detach();
  if (failures != 0) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
