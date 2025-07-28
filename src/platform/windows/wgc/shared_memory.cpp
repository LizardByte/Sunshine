
#include "shared_memory.h"

#include "misc_utils.h"
#include "src/utility.h"

#include <aclapi.h>
#include <chrono>
#include <combaseapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <sddl.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

// --- End of all class and function definitions ---
// Note: The base send() method will be implemented directly in WinPipe

namespace platf::dxgi {

  // Helper functions for proper string conversion
  std::string wide_to_utf8(const std::wstring &wstr) {
    if (wstr.empty()) {
      return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
  }

  std::wstring utf8_to_wide(const std::string &str) {
    if (str.empty()) {
      return std::wstring();
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), &wstrTo[0], size_needed);
    return wstrTo;
  }

  // Helper to initialize a security descriptor and build its DACL from explicit ACEs.
  bool init_sd_with_explicit_aces(SECURITY_DESCRIPTOR &desc, std::vector<EXPLICIT_ACCESS> &eaList, PACL *out_pacl) {
    if (!InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION)) {
      return false;
    }
    PACL rawDacl = nullptr;
    DWORD err = SetEntriesInAcl(static_cast<ULONG>(eaList.size()), eaList.data(), nullptr, &rawDacl);
    if (err != ERROR_SUCCESS) {
      return false;
    }
    if (!SetSecurityDescriptorDacl(&desc, TRUE, rawDacl, FALSE)) {
      LocalFree(rawDacl);
      return false;
    }
    *out_pacl = rawDacl;
    return true;
  }

  bool NamedPipeFactory::create_security_descriptor(SECURITY_DESCRIPTOR &desc, PACL *out_pacl) const {
    safe_token token;
    util::c_ptr<TOKEN_USER> tokenUser;
    safe_sid user_sid;
    safe_sid system_sid;
    safe_dacl pDacl;

    BOOL isSystem = platf::dxgi::is_running_as_system();

    BOOST_LOG(info) << "create_security_descriptor: isSystem=" << isSystem;

    if (isSystem) {
      token.reset(platf::dxgi::retrieve_users_token(false));
      BOOST_LOG(info) << "create_security_descriptor: Retrieved user token for SYSTEM service, token=" << token.get();
      if (!token) {
        BOOST_LOG(error) << "Failed to retrieve user token when running as SYSTEM";
        return false;
      }
    } else {
      HANDLE raw_token = nullptr;
      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        BOOST_LOG(error) << "OpenProcessToken failed in create_security_descriptor, error=" << GetLastError();
        return false;
      }
      token.reset(raw_token);
      BOOST_LOG(info) << "create_security_descriptor: Opened current process token, token=" << token.get();
    }

    // Extract user SID from token
    DWORD len = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      BOOST_LOG(error) << "GetTokenInformation (size query) failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }

    auto tokenBuffer = std::make_unique<uint8_t[]>(len);
    tokenUser.reset(reinterpret_cast<TOKEN_USER *>(tokenBuffer.release()));
    if (!tokenUser || !GetTokenInformation(token.get(), TokenUser, tokenUser.get(), len, &len)) {
      BOOST_LOG(error) << "GetTokenInformation (fetch) failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }
    PSID raw_user_sid = tokenUser->User.Sid;

    // Validate the user SID
    if (!IsValidSid(raw_user_sid)) {
      BOOST_LOG(error) << "Invalid user SID in create_security_descriptor";
      return false;
    }

    // Log the user SID for debugging
    if (LPWSTR sidString = nullptr; ConvertSidToStringSidW(raw_user_sid, &sidString)) {
      std::wstring wsid(sidString);
      BOOST_LOG(info) << "create_security_descriptor: User SID=" << wide_to_utf8(wsid);
      LocalFree(sidString);
    }

    // Always create SYSTEM SID for consistent permissions
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID raw_system_sid = nullptr;
    if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &raw_system_sid)) {
      BOOST_LOG(error) << "AllocateAndInitializeSid failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }
    system_sid.reset(raw_system_sid);

    // Validate the system SID
    if (!IsValidSid(system_sid.get())) {
      BOOST_LOG(error) << "Invalid system SID in create_security_descriptor";
      return false;
    }

    // Log the system SID for debugging
    if (LPWSTR systemSidString = nullptr; ConvertSidToStringSidW(system_sid.get(), &systemSidString)) {
      std::wstring wsid(systemSidString);
      BOOST_LOG(info) << "create_security_descriptor: System SID=" << wide_to_utf8(wsid);
      LocalFree(systemSidString);
    }

    // Initialize security descriptor
    if (!InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION)) {
      BOOST_LOG(error) << "InitializeSecurityDescriptor failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }

    // Build a list of explicit ACEs
    std::vector<EXPLICIT_ACCESS> eaList;
    if (isSystem) {
      EXPLICIT_ACCESS eaSys {};
      eaSys.grfAccessPermissions = GENERIC_ALL;
      eaSys.grfAccessMode = SET_ACCESS;
      eaSys.grfInheritance = NO_INHERITANCE;
      eaSys.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      eaSys.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
      eaSys.Trustee.ptstrName = (LPTSTR) system_sid.get();
      eaList.push_back(eaSys);
      EXPLICIT_ACCESS eaUser = eaSys;
      eaUser.Trustee.TrusteeType = TRUSTEE_IS_USER;
      eaUser.Trustee.ptstrName = (LPTSTR) raw_user_sid;
      eaList.push_back(eaUser);
    }
    if (!eaList.empty()) {
      if (!init_sd_with_explicit_aces(desc, eaList, out_pacl)) {
        BOOST_LOG(error) << "init_sd_with_explicit_aces failed in create_security_descriptor";
        return false;
      }
    }
    return true;
  }

  // --- NamedPipeFactory Implementation ---

  std::unique_ptr<INamedPipe> NamedPipeFactory::create_server(const std::string &pipeName) {
    BOOST_LOG(info) << "NamedPipeFactory::create_server called with pipeName='" << pipeName << "'";
    auto wPipeBase = utf8_to_wide(pipeName);
    std::wstring fullPipeName = (wPipeBase.find(LR"(\\.\pipe\)") == 0) ? wPipeBase : LR"(\\.\pipe\)" + wPipeBase;

    SECURITY_ATTRIBUTES *pSecAttr = nullptr;
    SECURITY_ATTRIBUTES secAttr {};
    SECURITY_DESCRIPTOR secDesc {};
    PACL rawDacl = nullptr;

    auto fg = util::fail_guard([&]() {
      if (rawDacl) {
        LocalFree(rawDacl);
      }
    });

    if (platf::dxgi::is_running_as_system()) {
      if (!create_security_descriptor(secDesc, &rawDacl)) {
        BOOST_LOG(error) << "Failed to init security descriptor";
        return nullptr;
      }
      secAttr = {sizeof(secAttr), &secDesc, FALSE};
      pSecAttr = &secAttr;
      BOOST_LOG(info) << "Security attributes prepared (isSystem=" << platf::dxgi::is_running_as_system() << ").";
    }

    safe_handle hPipe(CreateNamedPipeW(
      fullPipeName.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      65536,
      65536,
      0,
      pSecAttr
    ));
    if (!hPipe) {
      DWORD err = GetLastError();
      BOOST_LOG(error) << "CreateNamedPipeW failed (" << err << ")";
      return nullptr;
    }

    auto pipeObj = std::make_unique<WinPipe>(hPipe.release(), true);
    BOOST_LOG(info) << "Returning WinPipe (server) for '" << pipeName << "'";
    return pipeObj;
  }

  std::unique_ptr<INamedPipe> NamedPipeFactory::create_client(const std::string &pipeName) {
    BOOST_LOG(info) << "NamedPipeFactory::create_client called with pipeName='" << pipeName << "'";
    auto wPipeBase = utf8_to_wide(pipeName);
    std::wstring fullPipeName = (wPipeBase.find(LR"(\\.\pipe\)") == 0) ? wPipeBase : LR"(\\.\pipe\)" + wPipeBase;

    safe_handle hPipe = create_client_pipe(fullPipeName);
    if (!hPipe) {
      DWORD err = GetLastError();
      BOOST_LOG(error) << "CreateFileW failed (" << err << ")";
      return nullptr;
    }

    auto pipeObj = std::make_unique<WinPipe>(hPipe.release(), false);
    BOOST_LOG(info) << "Returning WinPipe (client) for '" << pipeName << "'";
    return pipeObj;
  }

  // Helper function to create client pipe with retry logic
  safe_handle NamedPipeFactory::create_client_pipe(const std::wstring &fullPipeName) const {
    const auto kTimeoutEnd = GetTickCount64() + 2000;  // 2s overall timeout instead of 5s for faster failure
    safe_handle hPipe;

    while (!hPipe && GetTickCount64() < kTimeoutEnd) {
      hPipe.reset(CreateFileW(
        fullPipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,  // ← always nullptr
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
      ));

      if (!hPipe) {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
          // Someone else already has the pipe – wait for a free instance
          if (!WaitNamedPipeW(fullPipeName.c_str(), 250)) {
            continue;
          }
        } else if (err == ERROR_FILE_NOT_FOUND) {
          // Server hasn't created the pipe yet – short back-off
          Sleep(50);
          continue;
        } else {
          BOOST_LOG(error) << "CreateFileW failed (" << err << ")";
          return safe_handle {};
        }
      }
    }
    return hPipe;
  }

  AnonymousPipeFactory::AnonymousPipeFactory():
      _pipe_factory(std::make_unique<NamedPipeFactory>()) {}

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::create_server(const std::string &pipeName) {
    DWORD pid = GetCurrentProcessId();
    std::string pipeNameWithPid = std::format("{}_{}", pipeName, pid);
    auto first_pipe = _pipe_factory->create_server(pipeNameWithPid);
    if (!first_pipe) {
      return nullptr;
    }
    return handshake_server(std::move(first_pipe));
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::create_client(const std::string &pipeName) {
    DWORD pid = platf::dxgi::get_parent_process_id();
    std::string pipeNameWithPid = std::format("{}_{}", pipeName, pid);
    auto first_pipe = _pipe_factory->create_client(pipeNameWithPid);
    if (!first_pipe) {
      return nullptr;
    }
    return handshake_client(std::move(first_pipe));
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::handshake_server(std::unique_ptr<INamedPipe> pipe) {
    std::string pipe_name = generate_guid();

    std::wstring wpipe_name = utf8_to_wide(pipe_name);

    AnonConnectMsg message {};
    wcsncpy_s(message.pipe_name, wpipe_name.c_str(), _TRUNCATE);

    std::vector<uint8_t> bytes(sizeof(AnonConnectMsg));
    std::memcpy(bytes.data(), &message, sizeof(AnonConnectMsg));

    pipe->wait_for_client_connection(3000);

    if (!pipe->is_connected()) {
      BOOST_LOG(error) << "Client did not connect to pipe instance within the specified timeout. Disconnecting server pipe.";
      pipe->disconnect();
      return nullptr;
    }
    BOOST_LOG(info) << "Sending handshake message to client with pipe_name='" << pipe_name;
    if (!pipe->send(bytes, 5000)) {  // 5 second timeout for handshake
      BOOST_LOG(error) << "Failed to send handshake message to client";
      pipe->disconnect();
      return nullptr;
    }

    // Wait for ACK from client before disconnecting
    std::vector<uint8_t> ack;
    bool ack_ok = false;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
      PipeResult result = pipe->receive(ack, 1000);  // 1 second timeout per receive attempt
      if (result == PipeResult::Success) {
        if (ack.size() == 1 && ack[0] == 0xA5) {
          ack_ok = true;
          BOOST_LOG(info) << "Received handshake ACK from client";
          break;
        }
        if (!ack.empty()) {
          BOOST_LOG(warning) << "Received unexpected data during ACK wait, size=" << ack.size();
        }
      } else if (result == PipeResult::BrokenPipe || result == PipeResult::Error || result == PipeResult::Disconnected) {
        BOOST_LOG(error) << "Pipe error during handshake ACK wait";
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!ack_ok) {
      BOOST_LOG(error) << "Handshake ACK not received within timeout - aborting";
      pipe->disconnect();
      return nullptr;
    }

    auto dataPipe = _pipe_factory->create_server(pipe_name);
    // Ensure the server side waits for a client
    if (dataPipe) {
      dataPipe->wait_for_client_connection(0);  // 0 = immediate IF connected, otherwise overlapped wait
    }

    pipe->disconnect();
    return dataPipe;
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::handshake_client(std::unique_ptr<INamedPipe> pipe) {
    AnonConnectMsg msg {};  // Zero-initialize

    std::vector<uint8_t> bytes;
    auto start = std::chrono::steady_clock::now();
    bool received = false;

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
      PipeResult result = pipe->receive(bytes, 500);  // 500ms timeout per receive attempt
      if (result == PipeResult::Success) {
        if (!bytes.empty()) {
          received = true;
          break;
        }
      } else if (result == PipeResult::BrokenPipe || result == PipeResult::Error || result == PipeResult::Disconnected) {
        BOOST_LOG(error) << "Pipe error during handshake message receive";
        break;
      }
      // Check if we received 0 bytes due to pipe being closed
      if (bytes.empty()) {
        BOOST_LOG(warning) << "Received 0 bytes during handshake - server may have closed pipe";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!received) {
      BOOST_LOG(error) << "Did not receive handshake message in time. Disconnecting client.";
      pipe->disconnect();
      return nullptr;
    }

    if (bytes.size() < sizeof(AnonConnectMsg)) {
      BOOST_LOG(error) << "Received incomplete handshake message (size=" << bytes.size()
                       << ", expected=" << sizeof(AnonConnectMsg) << "). Disconnecting client.";
      pipe->disconnect();
      return nullptr;
    }

    std::memcpy(&msg, bytes.data(), sizeof(AnonConnectMsg));

    // Send ACK (1 byte) with timeout
    BOOST_LOG(info) << "Sending handshake ACK to server";
    if (!pipe->send({ACK_MSG}, 5000)) {  // 5 second timeout for ACK
      BOOST_LOG(error) << "Failed to send handshake ACK to server";
      pipe->disconnect();
      return nullptr;
    }

    // Flush the control pipe to ensure ACK is delivered before disconnecting
    static_cast<WinPipe *>(pipe.get())->flush_buffers();

    // Convert wide string to string using proper conversion
    std::wstring wpipeName(msg.pipe_name);
    std::string pipeNameStr = wide_to_utf8(wpipeName);
    // Disconnect control pipe only after ACK is sent and flushed
    pipe->disconnect();

    // Retry logic for opening the data pipe
    std::unique_ptr<INamedPipe> data_pipe = nullptr;
    auto retry_start = std::chrono::steady_clock::now();
    const auto retry_timeout = std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() - retry_start < retry_timeout) {
      data_pipe = _pipe_factory->create_client(pipeNameStr);
      if (data_pipe) {
        break;
      }
      BOOST_LOG(info) << "Retrying data pipe connection...";
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!data_pipe) {
      BOOST_LOG(error) << "Failed to connect to data pipe after retries";
      return nullptr;
    }

    return data_pipe;
  }

  std::string AnonymousPipeFactory::generate_guid() const {
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK) {
      return {};
    }

    std::array<WCHAR, 39> guidStr {};  // "{...}" format, 38 chars + null
    if (StringFromGUID2(guid, guidStr.data(), 39) == 0) {
      return {};
    }

    std::wstring wstr(guidStr.data());
    return wide_to_utf8(wstr);
  }

  // --- WinPipe Implementation ---
  WinPipe::WinPipe(HANDLE pipe, bool isServer):
      _pipe(pipe),
      _connected(false),
      _is_server(isServer) {
    if (!_is_server && _pipe != INVALID_HANDLE_VALUE) {
      _connected.store(true, std::memory_order_release);
      BOOST_LOG(info) << "WinPipe (client): Connected immediately after CreateFileW, handle valid, mode set.";
    }
  }

  WinPipe::~WinPipe() {
    WinPipe::disconnect();
  }

  bool WinPipe::send(std::vector<uint8_t> bytes, int timeout_ms) {
    if (!_connected.load(std::memory_order_acquire) || _pipe == INVALID_HANDLE_VALUE) {
      return false;
    }

    auto ctx = std::make_unique<io_context>();
    if (!ctx->is_valid()) {
      BOOST_LOG(error) << "Failed to create I/O context for send operation, error=" << GetLastError();
      return false;
    }

    DWORD bytesWritten = 0;
    if (BOOL result = WriteFile(_pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, ctx->get()); !result) {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        // Wait for completion with timeout
        BOOST_LOG(info) << "WriteFile is pending, waiting for completion with timeout=" << timeout_ms << "ms.";
        DWORD waitResult = WaitForSingleObject(ctx->event(), timeout_ms);
        if (waitResult == WAIT_OBJECT_0) {
          if (!GetOverlappedResult(_pipe, ctx->get(), &bytesWritten, FALSE)) {
            BOOST_LOG(error) << "GetOverlappedResult failed in send, error=" << GetLastError();
            return false;
          }
        } else if (waitResult == WAIT_TIMEOUT) {
          BOOST_LOG(warning) << "Send operation timed out after " << timeout_ms << "ms";
          CancelIoEx(_pipe, ctx->get());
          // Wait for cancellation to complete to ensure OVERLAPPED structure safety
          DWORD transferred = 0;
          GetOverlappedResult(_pipe, ctx->get(), &transferred, TRUE);
          return false;
        } else {
          BOOST_LOG(error) << "WaitForSingleObject failed in send, result=" << waitResult << ", error=" << GetLastError();
          return false;
        }
      } else {
        BOOST_LOG(error) << "WriteFile failed (" << err << ") in WinPipe::send";
        return false;
      }
    }
    if (bytesWritten != bytes.size()) {
      BOOST_LOG(error) << "WriteFile wrote " << bytesWritten << " bytes, expected " << bytes.size();
      return false;
    }
    return true;
  }

  PipeResult WinPipe::receive(std::vector<uint8_t> &bytes, int timeout_ms) {
    bytes.clear();
    if (!_connected.load(std::memory_order_acquire) || _pipe == INVALID_HANDLE_VALUE) {
      return PipeResult::Disconnected;
    }

    auto ctx = std::make_unique<io_context>();
    if (!ctx->is_valid()) {
      BOOST_LOG(error) << "Failed to create I/O context for receive operation, error=" << GetLastError();
      return PipeResult::Error;
    }

    std::vector<uint8_t> buffer(4096);
    DWORD bytesRead = 0;
    BOOL result = ReadFile(_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, ctx->get());

    if (result) {
      // Read completed immediately
      buffer.resize(bytesRead);
      bytes = std::move(buffer);
      return PipeResult::Success;
    } else {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        // Wait for completion with timeout
        DWORD waitResult = WaitForSingleObject(ctx->event(), timeout_ms);
        if (waitResult == WAIT_OBJECT_0) {
          if (GetOverlappedResult(_pipe, ctx->get(), &bytesRead, FALSE)) {
            buffer.resize(bytesRead);
            bytes = std::move(buffer);
            return PipeResult::Success;
          } else {
            DWORD overlappedErr = GetLastError();
            if (overlappedErr == ERROR_BROKEN_PIPE) {
              BOOST_LOG(warning) << "Pipe broken during receive operation (ERROR_BROKEN_PIPE)";
              return PipeResult::BrokenPipe;
            }
            BOOST_LOG(error) << "GetOverlappedResult failed in receive, error=" << overlappedErr;
            return PipeResult::Error;
          }
        } else if (waitResult == WAIT_TIMEOUT) {
          CancelIoEx(_pipe, ctx->get());
          // Wait for cancellation to complete to ensure OVERLAPPED structure safety
          DWORD transferred = 0;
          GetOverlappedResult(_pipe, ctx->get(), &transferred, TRUE);
          return PipeResult::Timeout;
        } else {
          BOOST_LOG(error) << "WinPipe::receive() wait failed, result=" << waitResult << ", error=" << GetLastError();
          return PipeResult::Error;
        }
      } else if (err == ERROR_BROKEN_PIPE) {
        BOOST_LOG(warning) << "Pipe broken during ReadFile (ERROR_BROKEN_PIPE)";
        return PipeResult::BrokenPipe;
      } else {
        BOOST_LOG(error) << "ReadFile failed in receive, error=" << err;
        return PipeResult::Error;
      }
    }
  }

  void WinPipe::disconnect() {
    // Cancel any pending I/O operations (from any thread)
    if (_pipe != INVALID_HANDLE_VALUE) {
      CancelIoEx(_pipe, nullptr);
    }

    if (_pipe != INVALID_HANDLE_VALUE) {
      if (_is_server) {
        // Ensure any final writes are delivered before closing (rare edge-case)
        FlushFileBuffers(_pipe);
        DisconnectNamedPipe(_pipe);
        BOOST_LOG(info) << "WinPipe (server): Disconnected via DisconnectNamedPipe.";
      } else {
        BOOST_LOG(info) << "WinPipe (client): Disconnected (no DisconnectNamedPipe).";
      }
      CloseHandle(_pipe);
      _pipe = INVALID_HANDLE_VALUE;
    }
    _connected.store(false, std::memory_order_release);
    BOOST_LOG(info) << "AsyncPipe: Connection state set to false (disconnected).";
  }

  void WinPipe::wait_for_client_connection(int milliseconds) {
    if (_pipe == INVALID_HANDLE_VALUE) {
      return;
    }

    if (_is_server) {
      // For server pipes, use ConnectNamedPipe with proper overlapped I/O
      connect_server_pipe(milliseconds);
    } else {
      // For client handles created with CreateFileW, the connection already exists
      // _connected is set in constructor
    }
  }

  void WinPipe::connect_server_pipe(int milliseconds) {
    auto ctx = std::make_unique<io_context>();
    if (!ctx->is_valid()) {
      BOOST_LOG(error) << "Failed to create I/O context for connection, error=" << GetLastError();
      return;
    }

    BOOL result = ConnectNamedPipe(_pipe, ctx->get());
    if (result) {
      _connected = true;
      BOOST_LOG(info) << "WinPipe (server): Connected after ConnectNamedPipe returned true.";
    } else {
      DWORD err = GetLastError();
      if (err == ERROR_PIPE_CONNECTED) {
        // Client already connected
        _connected = true;
      } else if (err == ERROR_IO_PENDING) {
        // Wait for the connection to complete
        DWORD waitResult = WaitForSingleObject(ctx->event(), milliseconds > 0 ? milliseconds : 5000);  // Use param or default 5s
        if (waitResult == WAIT_OBJECT_0) {
          DWORD transferred = 0;
          if (GetOverlappedResult(_pipe, ctx->get(), &transferred, FALSE)) {
            _connected = true;
            BOOST_LOG(info) << "WinPipe (server): Connected after overlapped ConnectNamedPipe completed";
          } else {
            BOOST_LOG(error) << "GetOverlappedResult failed in connect, error=" << GetLastError();
          }
        } else if (waitResult == WAIT_TIMEOUT) {
          BOOST_LOG(error) << "ConnectNamedPipe timeout after " << (milliseconds > 0 ? milliseconds : 5000) << "ms";
          CancelIoEx(_pipe, ctx->get());
          // Wait for cancellation to complete to ensure OVERLAPPED structure safety
          DWORD transferred = 0;
          GetOverlappedResult(_pipe, ctx->get(), &transferred, TRUE);
        } else {
          BOOST_LOG(error) << "ConnectNamedPipe wait failed, waitResult=" << waitResult << ", error=" << GetLastError();
        }
      } else {
        BOOST_LOG(error) << "ConnectNamedPipe failed, error=" << err;
      }
    }
  }

  bool WinPipe::is_connected() {
    return _connected.load(std::memory_order_acquire);
  }

  void WinPipe::flush_buffers() {
    if (_pipe != INVALID_HANDLE_VALUE) {
      FlushFileBuffers(_pipe);
    }
  }

  // --- AsyncNamedPipe Implementation ---
  AsyncNamedPipe::AsyncNamedPipe(std::unique_ptr<INamedPipe> pipe):
      _pipe(std::move(pipe)),
      _running(false) {
  }

  AsyncNamedPipe::~AsyncNamedPipe() {
    stop();
  }

  bool AsyncNamedPipe::start(const MessageCallback &onMessage, const ErrorCallback &onError, const BrokenPipeCallback &onBrokenPipe) {
    if (_running.load(std::memory_order_acquire)) {
      return false;  // Already running
    }

    if (!_pipe) {
      if (onError) {
        onError("No pipe available - failed to create pipe");
      }
      return false;
    }

    _onMessage = onMessage;
    _onError = onError;
    _onBrokenPipe = onBrokenPipe;

    _running.store(true, std::memory_order_release);
    _worker = std::thread(&AsyncNamedPipe::worker_thread, this);
    return true;
  }

  void AsyncNamedPipe::stop() {
    _running.store(false, std::memory_order_release);

    // Cancel any pending I/O operations to unblock the worker thread
    if (_pipe) {
      _pipe->disconnect();
    }

    if (_worker.joinable()) {
      _worker.join();
    }
  }

  void AsyncNamedPipe::send(const std::vector<uint8_t> &message) {
    safe_execute_operation("send", [this, &message]() {
      if (_pipe && _pipe->is_connected()) {
        if (!_pipe->send(message, 5000)) {  // 5 second timeout for async sends
          BOOST_LOG(warning) << "Failed to send message through AsyncNamedPipe (timeout or error)";
        }
      }
    });
  }

  void AsyncNamedPipe::wait_for_client_connection(int milliseconds) {
    _pipe->wait_for_client_connection(milliseconds);
  }

  bool AsyncNamedPipe::is_connected() const {
    return _pipe && _pipe->is_connected();
  }

  void AsyncNamedPipe::safe_execute_operation(const std::string &operation_name, const std::function<void()> &operation) const noexcept {
    if (!operation) {
      return;
    }

    try {
      operation();
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "AsyncNamedPipe: Exception in " << operation_name << ": " << e.what();
    } catch (...) {
      BOOST_LOG(error) << "AsyncNamedPipe: Unknown exception in " << operation_name;
    }
  }

  void AsyncNamedPipe::worker_thread() noexcept {
    safe_execute_operation("worker_thread", [this]() {
      if (!establish_connection()) {
        return;
      }

      run_message_loop();
    });
  }

  void AsyncNamedPipe::run_message_loop() {
    using namespace std::chrono_literals;

    for (; _running.load(std::memory_order_acquire);) {
      std::vector<uint8_t> message;
      // The timeout here is why we don't need a sleep each loop.
      // Eventually the message queue empties and its forced to wait.
      PipeResult res = _pipe->receive(message, 1000);  // 1 s timeout

      // Fast cancel – bail out even before decoding res
      if (!_running.load(std::memory_order_acquire)) {
        break;
      }

      switch (res) {
        case PipeResult::Success:
          {
            if (message.empty()) {  // remote closed
              BOOST_LOG(info)
                << "AsyncNamedPipe: remote closed pipe";
              return;
            }
            process_message(std::move(message));
            break;  // keep looping
          }

        case PipeResult::Timeout:
          break;  // nothing to do

        case PipeResult::BrokenPipe:
          safe_execute_operation("brokenPipe callback", _onBrokenPipe);
          return;  // terminate

        case PipeResult::Error:
        case PipeResult::Disconnected:
        default:
          return;  // terminate
      }
    }
  }

  bool AsyncNamedPipe::establish_connection() {
    // For server pipes, we need to wait for a client connection first
    if (!_pipe || _pipe->is_connected()) {
      return true;
    }

    _pipe->wait_for_client_connection(5000);  // Wait up to 5 seconds for connection
    if (!_pipe->is_connected()) {
      BOOST_LOG(error) << "AsyncNamedPipe: Failed to establish connection within timeout";
      safe_execute_operation("error callback", [this]() {
        if (_onError) {
          _onError("Failed to establish connection within timeout");
        }
      });
      return false;
    }
    return _pipe->is_connected();
  }

  void AsyncNamedPipe::process_message(const std::vector<uint8_t> &bytes) const {
    if (!_onMessage) {
      return;
    }

    safe_execute_operation("message callback", [this, &bytes]() {
      _onMessage(bytes);
    });
  }
}  // namespace platf::dxgi
