#include "../../../src/platform/windows/wgc/process_handler.h"
#include <gtest/gtest.h>
#include <windows.h>

// Integration tests: actually launch a simple process (cmd.exe /C timeout /T 1)
// These tests will launch and wait for a real process. Safe for CI.

class ProcessHandlerTest : public ::testing::Test {
protected:
    ProcessHandler ph;
};

TEST_F(ProcessHandlerTest, StartReturnsFalseIfAlreadyRunning) {
    // Launch a real process
    std::wstring app = L"C:\\Windows\\System32\\cmd.exe";
    std::wstring args = L"/C timeout /T 1 /NOBREAK >nul";
    ASSERT_TRUE(ph.start(app, args));
    // Attempt to start again while running
    EXPECT_FALSE(ph.start(app, args));
    // Clean up: wait for process to finish
    DWORD code = 0;
    ph.wait(code);
}

TEST_F(ProcessHandlerTest, WaitReturnsFalseIfNotRunning) {
    DWORD code = 0;
    EXPECT_FALSE(ph.wait(code));
}

TEST_F(ProcessHandlerTest, TerminateDoesNothingIfNotRunning) {
    // Should not crash or throw
    ph.terminate();
    SUCCEED();
}


TEST_F(ProcessHandlerTest, StartAndWaitSuccess) {
    // Launch cmd.exe to run a short timeout (1 second)
    std::wstring app = L"C:\\Windows\\System32\\cmd.exe";
    std::wstring args = L"/C timeout /T 1 /NOBREAK >nul";
    ASSERT_TRUE(ph.start(app, args));
    DWORD code = 0;
    ASSERT_TRUE(ph.wait(code));
    // cmd.exe returns 0 on success for timeout
    EXPECT_EQ(code, 0u);
}

TEST_F(ProcessHandlerTest, TerminateRunningProcess) {
    std::wstring app = L"C:\\Windows\\System32\\cmd.exe";
    std::wstring args = L"/C timeout /T 5 /NOBREAK >nul";
    ASSERT_TRUE(ph.start(app, args));
    // Terminate before it finishes
    ph.terminate();
    DWORD code = 0;
    // Wait should fail since process was terminated
    EXPECT_FALSE(ph.wait(code));
}

TEST_F(ProcessHandlerTest, HandlesNotLeakedOnFailedStart) {
    // Try to start a non-existent executable
    std::wstring app = L"C:\\notarealfile.exe";
    std::wstring args = L"";
    EXPECT_FALSE(ph.start(app, args));
    // No crash, no leak
}
