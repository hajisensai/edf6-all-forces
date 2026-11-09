// The in-game loadout menu's file and process side, free of the game (tests/loadout_writer_test.cpp runs it offline):
// whole-file reads that never block a writer, the atomic replace the ini is saved with, and the background variant
// generator (the installer's `loadout-generate`, tools/installer.py) with the status file it leaves
// (Mods/Plugins/EDF6VehicleCrew.generate_status.txt). src/loadout_store.cpp wires these to the plugin's paths.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace crew {
// The whole file into *out. Opened sharing read / write / delete, so the generator's rename over it never fails.
bool ReadFileBytes(const wchar_t* path,std::string* out,FILETIME* written=nullptr) noexcept;
// `bytes` to `path` through a temporary file beside it and MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH): a reader sees
// the old file or the new one, never a part. False (the temporary removed, `path` untouched) on any failure.
bool ReplaceFileAtomic(const wchar_t* path,const std::string& bytes) noexcept;

enum class GenerateState : std::uint8_t { idle, running, ok, failed, missing };
struct GenerateStatus {
    GenerateState state=GenerateState::idle;
    int made=0,failed=0;
    wchar_t message[200]{};
};
// The status file's text (UTF-8 lines key=value: state=running|ok|failed, made=, failed=, message=). Unknown or absent
// state: idle.
GenerateStatus ParseGenerateStatus(const std::string& text) noexcept;

// One background generator at a time: `exe` started with `args` hidden (CREATE_NO_WINDOW, no handles inherited, below
// normal priority). Status() reads the status file it writes; a generator that exited without a final state (crashed,
// killed) or a stale `running` left by one this object never started is reported failed, never running forever.
class VariantGenerator {
public:
    // The plugin's generator runs below normal priority, so it never takes the game's CPU. tests/loadout_writer_test.cpp
    // starts its stand-in children at normal priority: what it checks is the process's lifecycle and status file, and
    // a below-normal child on a machine kept busy at normal priority (ctest -j beside other sessions) gets the CPU only
    // from the balance-set manager's starvation boost, so it ran for minutes to past the 900 s timeout. The test
    // checks the default's class itself on a child it does not wait for (PriorityClass).
    explicit VariantGenerator(DWORD priorityClass=BELOW_NORMAL_PRIORITY_CLASS) noexcept : priority_(priorityClass) {}
    // False when one is still running or the process could not start (`why` says which).
    bool Start(const wchar_t* exe,const wchar_t* args,wchar_t* why,std::size_t capacity) noexcept;
    GenerateStatus Status(const wchar_t* statusPath) noexcept;
    bool Running() noexcept;
    // The priority class of the process last started and not yet reaped (it answers after the process has exited);
    // 0 with none.
    DWORD PriorityClass() const noexcept { return process_ ? GetPriorityClass(process_) : 0; }
    ~VariantGenerator();
private:
    void Reap() noexcept;
    DWORD priority_;
    HANDLE process_=nullptr;
    bool ran_=false;
    DWORD exitCode_=0;
    FILETIME started_{};   // the process's creation time: older status files belong to a previous run
};
}  // namespace crew
