#pragma once
#include <string>
#include <windows.h>

class ProcessHandler {
public:
    // Starts a process with the given application path and arguments.
    // Returns true on success, false on failure.
    bool start(const std::wstring& application, std::wstring_view arguments);

    // Waits for the process to finish and returns the exit code.
    // Returns true if the process finished successfully, false otherwise.
    bool wait(DWORD& exitCode);

    // Terminates the process if running.
    void terminate();

    // Cleans up handles.
    ~ProcessHandler();

private:
    PROCESS_INFORMATION pi_{};
    bool running_ = false;
};
