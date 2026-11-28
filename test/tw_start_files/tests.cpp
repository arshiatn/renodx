// The 64-bit CPU page's start files (src/games/twattila/start_files.hpp) over sample files in a temp folder (never a
// game folder): settings.txt as Sol's loader 1.3.28 reads and writes it (S28 = Sol's src_1328 attila64\loader\), and
// heapfix.ini as the Engine Performance Fix keeps it (the profile API).
// 1. Sol's read tests, one case each: task_waits only exactly "fast" (S28 picker.c:184-187), the name in either case
//    (:63); texture_uploads only exactly "full" (:188-191); perf_fix / flag_wave off only on a leading '0' (:205-212);
//    shadow_refresh one character 1-4 (:271-275); interface_size Auto / percents (:94-101); upscaling's FrameBridge
//    fallback, any addon=off line (:253-269), its row only with FrameBridge in the game folder or the Workshop copy
//    (:236-251); the fix's three rows only while the fix is installed, its two parts read-only while perf_fix reads off
//    (:1262-1283); a missing key's "(default)".
// 2. The loader gate: Task Waits only from loader 1.3.24 (1.3.20 picker.c:177-180 read it the other way); no version:
//    hidden.
// 3. Sol's write (settings_put, S28 picker.c:72-92): no write when strcmp-equal, the key's older lines dropped (either
//    case), empty lines dropped, every other line kept in order, the lowercase name appended; the written spellings
//    read back (fast / game / rows); the .tmp replace (none left, a read-only file refused and kept).
// 4. Pending notes compare readings: fast -> Fast gives one, game -> GAME none.
// 5. Semi-automatic: settings.txt's other keys (0 / 1 Off / On, else read-only; the never-shown keys left out; a write
//    changes only that key); heapfix.ini's table keys (the file's section, defaults, read-only without a known
//    section), its other keys, flag_wave left to Sol's row, comments and order kept by a write, the file absent ->
//    read-only.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "src/games/twattila/start_files.hpp"

namespace start_files = twattila::start_files;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

namespace {

std::string ReadAll(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  std::stringstream text;
  text << stream.rdbuf();
  return text.str();
}

void WriteAll(const std::filesystem::path& file, std::string_view text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}

const start_files::FileRow* Find(const std::vector<start_files::FileRow>& rows, std::string_view key) {
  const auto row = std::ranges::find(rows, key, &start_files::FileRow::key);
  return (row == rows.end() ? nullptr : &*row);
}

}  // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / std::format("renodx_tw_start_files_{}", GetCurrentProcessId());
  std::filesystem::remove_all(root);
  const std::filesystem::path game = root / "steamapps" / "common" / "Attila";
  const std::filesystem::path exe_dir = game / "attila64";
  const std::filesystem::path settings = exe_dir / "settings.txt";
  const std::filesystem::path game_helper = game / "FrameBridge" / "helper64.exe";
  const std::filesystem::path workshop_helper =
      root / "steamapps" / "workshop" / "content" / "325610" / "3828791891" / "FrameBridge" / "helper64.exe";
  const std::optional<std::array<uint16_t, 4>> loader_1328 = std::array<uint16_t, 4>{1, 3, 28, 0};
  std::filesystem::create_directories(exe_dir);
  WriteAll(game_helper, "");
  // The rows of a settings.txt holding `text` (loader 1.3.28, the fix installed).
  const auto rows_of = [&](std::string_view text) {
    WriteAll(settings, text);
    return start_files::StartWindowRows(exe_dir, loader_1328, true);
  };
  const auto value_of = [&](std::string_view text, std::string_view key) {
    const std::vector<start_files::FileRow> rows = rows_of(text);
    const start_files::FileRow* row = Find(rows, key);
    return (row == nullptr ? -2 : row->value);
  };

  // 1. Sol's read tests.
  CHECK(value_of("Task_Waits=fast\n", "task_waits") == 1);
  CHECK(value_of("Task_Waits=game\n", "task_waits") == 0);
  CHECK(value_of("task_waits=Fast\n", "task_waits") == 0 && value_of("task_waits=GAME\n", "task_waits") == 0);
  CHECK(value_of("task_waits=\n", "task_waits") == 0 && value_of("x=1\n", "task_waits") == 0);
  CHECK(value_of("task_waits=fast\ntask_waits=game\n", "task_waits") == 0);  // the last line counts (:54-70)
  CHECK(value_of("texture_uploads=Full\n", "texture_uploads") == 0 && value_of("texture_uploads=full\n", "texture_uploads") == 1);
  CHECK(value_of("perf_fix=0x\n", "perf_fix") == 0 && value_of("perf_fix=yes\n", "perf_fix") == 1);
  CHECK(value_of("flag_wave=0\n", "flag_wave") == 0 && value_of("flag_wave=\n", "flag_wave") == 1);
  CHECK(value_of("shadow_refresh=12\n", "shadow_refresh") == 0 && value_of("shadow_refresh=4\n", "shadow_refresh") == 3);
  CHECK(value_of("shadow_refresh=0\n", "shadow_refresh") == 0 && value_of("shadow_refresh=2\n", "shadow_refresh") == 1);
  // INTERFACE_SIZES: 0 = 100%, 1 = Auto, 3 = 150%.
  CHECK(value_of("interface_size=AUTO\n", "interface_size") == 1 && value_of("interface_size=130\n", "interface_size") == 1);
  CHECK(value_of("interface_size=150\n", "interface_size") == 3 && value_of("interface_size=100\n", "interface_size") == 0);
  CHECK(value_of("interface_size=Like1080p\n", "interface_size") == 1 && value_of("x=1\n", "interface_size") == 1);
  CHECK(value_of("interface_size=150%\n", "interface_size") == 3 && value_of("interface_size=-150\n", "interface_size") == 1);
  // Upscaling with no line: FrameBridge's own settings, any addon=off line in either case.
  WriteAll(game / "FrameBridge" / "settings.txt", "frame_generation=on\nADDON=OFF\naddon=on\n");
  CHECK(value_of("x=1\n", "upscaling") == 0);
  WriteAll(game / "FrameBridge" / "settings.txt", "addon=on\n");
  CHECK(value_of("x=1\n", "upscaling") == 1 && value_of("upscaling=0\n", "upscaling") == 0);
  CHECK(value_of("upscaling=1\n", "upscaling") == 1 && value_of("upscaling=\n", "upscaling") == 1);
  std::filesystem::remove(game / "FrameBridge" / "settings.txt");
  CHECK(value_of("x=1\n", "upscaling") == 1);
  // The row only with FrameBridge: the game folder's or the Workshop copy.
  std::filesystem::remove(game_helper);
  CHECK(value_of("x=1\n", "upscaling") == -2);
  WriteAll(workshop_helper, "");
  CHECK(value_of("x=1\n", "upscaling") == 1);
  WriteAll(game_helper, "");
  // The fix's rows only while it is installed; its parts read-only while perf_fix reads off.
  {
    WriteAll(settings, "perf_fix=1\n");
    const std::vector<start_files::FileRow> without_fix = start_files::StartWindowRows(exe_dir, loader_1328, false);
    CHECK(Find(without_fix, "perf_fix") == nullptr && Find(without_fix, "flag_wave") == nullptr
          && Find(without_fix, "upscaling") == nullptr && Find(without_fix, "interface_size") != nullptr);
    const std::vector<start_files::FileRow> fix_off = rows_of("perf_fix=0\n");
    CHECK(Find(fix_off, "perf_fix")->editable && !Find(fix_off, "flag_wave")->editable && !Find(fix_off, "upscaling")->editable);
    const std::vector<start_files::FileRow> fix_on = rows_of("perf_fix=1\n");
    CHECK(Find(fix_on, "flag_wave")->editable && Find(fix_on, "upscaling")->editable);
    // A missing key: the owner's default, "(default)" in the tooltip; a key in the file: none.
    CHECK(Find(fix_on, "flag_wave")->tooltip.ends_with("\n(default)") && !Find(fix_on, "flag_wave")->text.has_value());
    CHECK(Find(fix_on, "perf_fix")->tooltip.find("(default)") == std::string::npos && Find(fix_on, "perf_fix")->text == "1");
    // The table's order: perf_fix before its parts.
    CHECK(Find(fix_on, "perf_fix") < Find(fix_on, "flag_wave") && Find(fix_on, "flag_wave") < Find(fix_on, "upscaling"));
  }
  std::printf("1. Sol's read tests ok\n");

  // 2. The loader gate.
  {
    WriteAll(settings, "task_waits=fast\n");
    // Hidden: no row (so nothing can write it), and not listed as another key either.
    const auto task_waits_shown = [&](const std::optional<std::array<uint16_t, 4>>& version) {
      const std::vector<start_files::FileRow> rows = start_files::StartWindowRows(exe_dir, version, true);
      return std::ranges::count(rows, std::string("task_waits"), &start_files::FileRow::key) == 1;
    };
    CHECK(!task_waits_shown(std::array<uint16_t, 4>{1, 3, 20, 0}) && !task_waits_shown(std::array<uint16_t, 4>{1, 3, 23, 9}));
    CHECK(task_waits_shown(std::array<uint16_t, 4>{1, 3, 24, 0}) && task_waits_shown(loader_1328));
    CHECK(!task_waits_shown(std::nullopt));
    const std::vector<start_files::FileRow> old_loader =
        start_files::StartWindowRows(exe_dir, std::array<uint16_t, 4>{1, 3, 20, 0}, true);
    CHECK(Find(old_loader, "interface_size") != nullptr && Find(old_loader, "texture_uploads") != nullptr);
  }
  std::printf("2. loader gate ok\n");

  // 3. Sol's write.
  const auto write = [&](std::string_view text, std::string_view key, int value) {
    std::vector<start_files::FileRow> rows = rows_of(text);
    const auto row = std::ranges::find(rows, key, &start_files::FileRow::key);
    if (row == rows.end()) return false;
    row->value = value;
    return start_files::Write(settings, *row);
  };
  CHECK(write("A=1\n\nTask_Waits=game\nx=2\r\nTASK_WAITS=Fast\ntask_waitsx=1\n", "task_waits", 1));
  CHECK(ReadAll(settings) == "A=1\r\nx=2\r\ntask_waitsx=1\r\ntask_waits=fast\r\n");
  CHECK(!std::filesystem::exists(exe_dir / "settings.txt.tmp"));
  // No write when strcmp-equal (the empty line would go).
  CHECK(write("task_waits=fast\n\nb=2\n", "task_waits", 1) && ReadAll(settings) == "task_waits=fast\n\nb=2\n");
  // Fast is not fast: written.
  CHECK(write("task_waits=Fast\n", "task_waits", 1) && ReadAll(settings) == "task_waits=fast\r\n");
  // The written spellings read back.
  CHECK(write("x=1\n", "task_waits", 0) && value_of(ReadAll(settings), "task_waits") == 0 && ReadAll(settings).find("task_waits=game") != std::string::npos);
  CHECK(write("x=1\n", "texture_uploads", 0) && ReadAll(settings).ends_with("texture_uploads=rows\r\n"));
  CHECK(value_of(ReadAll(settings), "texture_uploads") == 0);
  CHECK(write("x=1\n", "texture_uploads", 1) && value_of(ReadAll(settings), "texture_uploads") == 1);
  CHECK(write("x=1\n", "interface_size", 1) && ReadAll(settings).ends_with("interface_size=like1080p\r\n"));
  CHECK(write("x=1\n", "shadow_refresh", 2) && ReadAll(settings).ends_with("shadow_refresh=3\r\n"));
  CHECK(value_of(ReadAll(settings), "shadow_refresh") == 2);
  CHECK(write("x=1\n", "perf_fix", 0) && ReadAll(settings).ends_with("perf_fix=0\r\n") && value_of(ReadAll(settings), "perf_fix") == 0);
  // No file yet: made (as Sol's settings_put).
  std::filesystem::remove(settings);
  {
    std::vector<start_files::FileRow> rows = start_files::StartWindowRows(exe_dir, loader_1328, true);
    start_files::FileRow* row = &*std::ranges::find(rows, std::string("task_waits"), &start_files::FileRow::key);
    row->value = 1;
    CHECK(start_files::Write(settings, *row) && ReadAll(settings) == "task_waits=fast\r\n");
  }
  // A file that cannot be replaced: refused, the file as it was, no .tmp kept over it.
  WriteAll(settings, "task_waits=game\n");
  SetFileAttributesW(settings.c_str(), FILE_ATTRIBUTE_READONLY);
  {
    std::vector<start_files::FileRow> rows = start_files::StartWindowRows(exe_dir, loader_1328, true);
    start_files::FileRow* row = &*std::ranges::find(rows, std::string("task_waits"), &start_files::FileRow::key);
    row->value = 1;
    CHECK(!start_files::Write(settings, *row));
  }
  SetFileAttributesW(settings.c_str(), FILE_ATTRIBUTE_NORMAL);
  CHECK(ReadAll(settings) == "task_waits=game\n");
  std::filesystem::remove(exe_dir / "settings.txt.tmp");
  std::printf("3. Sol's write ok\n");

  // 4. Pending notes compare readings.
  {
    const auto pending = [&](std::string_view applied_text, std::string_view now_text) {
      const std::vector<start_files::FileRow> applied = rows_of(applied_text);
      const std::vector<start_files::FileRow> now = rows_of(now_text);
      return !Find(now, "task_waits")->ReadsAs(*Find(applied, "task_waits"));
    };
    CHECK(pending("task_waits=fast\n", "task_waits=Fast\n"));
    CHECK(!pending("task_waits=game\n", "task_waits=GAME\n") && !pending("x=1\n", "task_waits=game\n"));
    CHECK(pending("task_waits=game\n", "task_waits=fast\n") && !pending("task_waits=fast\n", "Task_Waits=fast\n"));
  }
  std::printf("4. pending notes ok\n");

  // 5. Semi-automatic: settings.txt's other keys.
  {
    const std::vector<start_files::FileRow> rows =
        rows_of("graphics=dx11\nshow_fps=1\nTheme=dark\nnew_key=1\nother=2\nNEW_KEY=0\ncursor_lock=off\nnoequals\n=5\nlast=1\n");
    const start_files::FileRow* new_key = Find(rows, "new_key");
    CHECK(new_key != nullptr && new_key->label == "new_key" && new_key->value == 0 && new_key->editable);
    const start_files::FileRow* other = Find(rows, "other");
    CHECK(other != nullptr && other->value == -1 && !other->editable && other->text == "2");
    CHECK(Find(rows, "graphics") == nullptr && Find(rows, "show_fps") == nullptr && Find(rows, "Theme") == nullptr
          && Find(rows, "cursor_lock") == nullptr && Find(rows, "NEW_KEY") == nullptr && Find(rows, "noequals") == nullptr);
    CHECK(rows.size() == 7 + 3 && rows.back().key == "last" && rows[7].key == "new_key" && rows[8].key == "other");
    // A write of another key changes only that key (Sol's rule: its lines replaced by one at the end).
    CHECK(write("graphics=dx11\nnew_key=1\nother=2\n", "new_key", 0) && ReadAll(settings) == "graphics=dx11\r\nother=2\r\nnew_key=0\r\n");
  }
  std::printf("5a. settings.txt's other keys ok\n");

  // 5. heapfix.ini.
  {
    const std::filesystem::path ini = exe_dir / "heapfix.ini";
    const std::string text =
        "; Engine Performance Fix settings. Restart the game after a change.\r\n[mod]\r\nself_update=0\r\n[fixes]\r\n"
        "; a comment\r\nmemory_allocator=0\r\nnew_fix=1\r\nodd=yes\r\n[tuning]\r\nculling=Strong\r\nmenu_textures_kept=64\r\n"
        "[other]\r\nflag_wave=1\r\nbattle_ui_cache=0\r\n";
    WriteAll(ini, text);
    std::vector<start_files::FileRow> rows = start_files::FixRows(ini);
    CHECK(rows.size() == start_files::FIX_KEYS.size() + 2);
    CHECK(Find(rows, "memory_allocator")->value == 0 && Find(rows, "memory_allocator")->section == "fixes");
    const start_files::FileRow* pools = Find(rows, "small_object_pools");
    CHECK(pools->value == 1 && pools->editable && pools->tooltip == "(default)" && !pools->text.has_value());
    CHECK(Find(rows, "culling")->value == 3 && Find(rows, "culling")->editable && Find(rows, "culling")->section == "tuning");
    CHECK(Find(rows, "menu_textures_kept")->value == 64 && Find(rows, "menu_textures_kept")->choices.empty());
    CHECK(Find(rows, "self_update")->value == 0 && Find(rows, "self_update")->section == "mod");
    // Not known: the default, or the section.
    CHECK(Find(rows, "crash_logging")->value == -1 && !Find(rows, "crash_logging")->editable);
    CHECK(Find(rows, "parallel_sort")->value == 1 && !Find(rows, "parallel_sort")->editable && Find(rows, "parallel_sort")->section.empty());
    // A table key in another section: the file's.
    CHECK(Find(rows, "battle_ui_cache")->section == "other" && Find(rows, "battle_ui_cache")->value == 0 && Find(rows, "battle_ui_cache")->editable);
    // Other keys, in the file's order; flag_wave left to Sol's row.
    CHECK(rows[start_files::FIX_KEYS.size()].key == "new_fix" && rows[start_files::FIX_KEYS.size()].value == 1
          && rows[start_files::FIX_KEYS.size()].editable && rows[start_files::FIX_KEYS.size()].section == "fixes");
    const start_files::FileRow* odd = Find(rows, "odd");
    CHECK(odd->value == -1 && !odd->editable && odd->text == "yes" && Find(rows, "flag_wave") == nullptr);
    // Counts outside 1-1024 or not a number: read-only.
    for (const char* bad : {"0", "2000", "12x", ""}) {
      WriteAll(ini, std::format("[tuning]\r\nmenu_textures_kept={}\r\n", bad));
      CHECK(Find(start_files::FixRows(ini), "menu_textures_kept")->value == -1);
    }
    WriteAll(ini, "[tuning]\r\nmenu_textures_kept=1024\r\n");
    CHECK(Find(start_files::FixRows(ini), "menu_textures_kept")->value == 1024);
    // Writes: one key, comments and order kept; a missing key added to its section.
    WriteAll(ini, text);
    rows = start_files::FixRows(ini);
    start_files::FileRow culling = *Find(rows, "culling");
    culling.value = 0;
    CHECK(start_files::Write(ini, culling));
    std::string expected = text;
    expected.replace(expected.find("culling=Strong"), 14, "culling=off");
    CHECK(ReadAll(ini) == expected);
    start_files::FileRow count = *Find(rows, "menu_textures_kept");
    count.value = 512;
    CHECK(start_files::Write(ini, count));
    expected.replace(expected.find("menu_textures_kept=64"), 21, "menu_textures_kept=512");
    CHECK(ReadAll(ini) == expected);
    start_files::FileRow added = *Find(rows, "small_object_pools");
    added.value = 0;
    CHECK(start_files::Write(ini, added));
    expected.replace(expected.find("odd=yes\r\n") + 9, 0, "small_object_pools=0\r\n");
    CHECK(ReadAll(ini) == expected);
    start_files::FileRow new_fix = *Find(rows, "new_fix");
    new_fix.value = 0;
    CHECK(start_files::Write(ini, new_fix));
    expected.replace(expected.find("new_fix=1"), 9, "new_fix=0");
    CHECK(ReadAll(ini) == expected);
    rows = start_files::FixRows(ini);
    CHECK(Find(rows, "culling")->value == 0 && Find(rows, "small_object_pools")->value == 0 && Find(rows, "new_fix")->value == 0);
    // Pending: readings, not spellings.
    WriteAll(ini, "[tuning]\r\nculling=light\r\n[fixes]\r\nmemory_allocator=1\r\n");
    const std::vector<start_files::FileRow> applied = start_files::FixRows(ini);
    WriteAll(ini, "[tuning]\r\nculling=LIGHT\r\n[fixes]\r\nmemory_allocator=0\r\nsmall_object_pools=1\r\n");
    const std::vector<start_files::FileRow> now = start_files::FixRows(ini);
    CHECK(Find(now, "culling")->ReadsAs(*Find(applied, "culling")) && Find(now, "small_object_pools")->ReadsAs(*Find(applied, "small_object_pools")));
    CHECK(!Find(now, "memory_allocator")->ReadsAs(*Find(applied, "memory_allocator")));
    // The file absent: every row read-only, no other keys.
    std::filesystem::remove(ini);
    rows = start_files::FixRows(ini);
    CHECK(rows.size() == start_files::FIX_KEYS.size()
          && std::ranges::none_of(rows, &start_files::FileRow::editable) && Find(rows, "culling")->value == 1);
    CHECK(!std::filesystem::exists(ini));
  }
  std::printf("5b. heapfix.ini ok\n");

  std::filesystem::remove_all(root);
  std::printf("tw_start_files: PASS\n");
  return 0;
}
