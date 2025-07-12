#include "process_handler.h"
#include <vector>
#include <algorithm>

bool ProcessHandler::start(const std::wstring& application, const std::wstring& arguments) {
    if (running_) return false;
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi_, sizeof(pi_));

    // Build command line: app path + space + arguments
    std::wstring cmd = application;
    if (!arguments.empty()) {
        cmd += L" ";
        cmd += arguments;
    }
    // Ensure null-terminated for CreateProcessW
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(0);

    BOOL result = CreateProcessW(
        application.c_str(), // lpApplicationName
        cmdline.data(),      // lpCommandLine (can be modified by CreateProcessW)
        nullptr,             // lpProcessAttributes
        nullptr,             // lpThreadAttributes
        FALSE,               // bInheritHandles
        0,                   // dwCreationFlags
        nullptr,             // lpEnvironment
        nullptr,             // lpCurrentDirectory
        &si,                 // lpStartupInfo
        &pi_                 // lpProcessInformation
    );
    running_ = (result != 0);
    if (!running_) {
        ZeroMemory(&pi_, sizeof(pi_));
    }
    return running_;
}

bool ProcessHandler::wait(DWORD& exitCode) {
    if (!running_ || pi_.hProcess == nullptr) return false;
    DWORD waitResult = WaitForSingleObject(pi_.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0) return false;
    BOOL gotCode = GetExitCodeProcess(pi_.hProcess, &exitCode);
    running_ = false;
    return gotCode != 0;
}

void ProcessHandler::terminate() {
    if (running_ && pi_.hProcess) {
        TerminateProcess(pi_.hProcess, 1);
        running_ = false;
    }
}

ProcessHandler::~ProcessHandler() {
    if (pi_.hProcess) CloseHandle(pi_.hProcess);
    if (pi_.hThread) CloseHandle(pi_.hThread);
}
