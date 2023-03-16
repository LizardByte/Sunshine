#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
  if(argc < 2) {
    std::cout << "Usage: " << argv[0] << " <command> [arguments]" << std::endl;
    return 1;
  }

  std::wstring command = std::wstring(argv[1], argv[1] + strlen(argv[1]));
  std::wstring arguments;

  for(int i = 2; i < argc; ++i) {
    arguments += std::wstring(argv[i], argv[i] + strlen(argv[i]));
    if(i < argc - 1) {
      arguments += L" ";
    }
  }

  SHELLEXECUTEINFOW info = { sizeof(SHELLEXECUTEINFOW) };
  info.lpVerb            = L"runas";
  info.lpFile            = command.c_str();
  info.lpParameters      = arguments.empty() ? NULL : arguments.c_str();
  info.nShow             = SW_SHOW;
  info.fMask             = SEE_MASK_NOCLOSEPROCESS;

  if(!ShellExecuteExW(&info)) {
    std::cout << "Error: ShellExecuteExW failed with code " << GetLastError() << std::endl;
    return 1;
  }

  WaitForSingleObject(info.hProcess, INFINITE);

  DWORD exitCode = 0;
  if(!GetExitCodeProcess(info.hProcess, &exitCode)) {
    std::cout << "Error: GetExitCodeProcess failed with code " << GetLastError() << std::endl;
  }

  CloseHandle(info.hProcess);

  return exitCode;
}
