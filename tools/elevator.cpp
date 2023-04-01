#include <Windows.h>
#include <iostream>
#include <string>

/**
 * @file elevator.cpp
 * @brief A simple command line utility to run a given command with administrative privileges.
 *
 * This utility helps run a command with administrative privileges on Windows
 * by leveraging the ShellExecuteExW function. The program accepts a command
 * and optional arguments, then attempts to run the command with elevated
 * privileges. If successful, it waits for the process to complete and
 * returns the exit code of the launched process.
 *
 * @example
 * To run the command prompt with administrative privileges, execute the following command:
 *   elevator.exe cmd
 *
 * To run a command, such as 'ipconfig /flushdns', with administrative privileges, execute:
 *   elevator.exe cmd /C "ipconfig /flushdns"
 */
int
main(int argc, char *argv[]) {
  // Check if the user provided at least one argument (the command to run)
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " <command> [arguments]" << std::endl;
    return 1;
  }

  // Convert the command and arguments from char* to wstring for use with ShellExecuteExW
  std::wstring command = std::wstring(argv[1], argv[1] + strlen(argv[1]));
  std::wstring arguments;

  // Concatenate the remaining arguments (if any) into a single wstring
  for (int i = 2; i < argc; ++i) {
    arguments += std::wstring(argv[i], argv[i] + strlen(argv[i]));
    if (i < argc - 1) {
      arguments += L" ";
    }
  }

  // Prepare the SHELLEXECUTEINFOW structure with the necessary information
  SHELLEXECUTEINFOW info = { sizeof(SHELLEXECUTEINFOW) };
  info.lpVerb = L"runas";  // Request elevation
  info.lpFile = command.c_str();
  info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  info.nShow = SW_SHOW;
  info.fMask = SEE_MASK_NOCLOSEPROCESS;  // So we can wait for the process to finish

  // Attempt to execute the command with elevation
  if (!ShellExecuteExW(&info)) {
    std::cout << "Error: ShellExecuteExW failed with code " << GetLastError() << std::endl;
    return 1;
  }

  // Wait for the launched process to finish
  WaitForSingleObject(info.hProcess, INFINITE);

  DWORD exitCode = 0;

  // Retrieve the exit code of the launched process
  if (!GetExitCodeProcess(info.hProcess, &exitCode)) {
    std::cout << "Error: GetExitCodeProcess failed with code " << GetLastError() << std::endl;
  }

  // Close the process handle
  CloseHandle(info.hProcess);

  // Return the exit code of the launched process
  return exitCode;
}
