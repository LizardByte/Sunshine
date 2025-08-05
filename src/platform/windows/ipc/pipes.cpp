

/**
 * @file pipes.cpp
 * @brief Implements Windows named and anonymous pipe IPC for Sunshine.
 *
 * Provides cross-process communication using Windows named pipes, including security descriptor setup,
 * overlapped I/O, and handshake logic for anonymous pipes. Used for secure and robust IPC between processes.
 */

// standard includes
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// platform includes
#include <AclAPI.h>
#include <combaseapi.h>
#include <sddl.h>
#include <Windows.h>

// local includes
#include "misc_utils.h"
#include "pipes.h"
#include "src/utility.h"

namespace platf::dxgi {

  // Helper functions for proper string conversion

  bool init_sd_with_explicit_aces(SECURITY_DESCRIPTOR &desc, std::vector<EXPLICIT_ACCESS> &eaList, PACL *out_pacl) {
    if (!InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION)) {
      return false;
    }
    PACL rawDacl = nullptr;
    if (DWORD err = SetEntriesInAcl(static_cast<ULONG>(eaList.size()), eaList.data(), nullptr, &rawDacl); err != ERROR_SUCCESS) {
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
    BOOL isSystem = platf::dxgi::is_running_as_system();

    safe_token token;
    if (!obtain_access_token(isSystem, token)) {
      return false;
    }

    util::c_ptr<TOKEN_USER> tokenUser;
    PSID raw_user_sid = nullptr;
    if (!extract_user_sid_from_token(token, tokenUser, raw_user_sid)) {
      return false;
    }

    safe_sid system_sid;
    if (!create_system_sid(system_sid)) {
      return false;
    }

    if (!InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION)) {
      BOOST_LOG(error) << "InitializeSecurityDescriptor failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }

    return build_access_control_list(isSystem, desc, raw_user_sid, system_sid.get(), out_pacl);
  }

  bool NamedPipeFactory::obtain_access_token(BOOL isSystem, safe_token &token) const {
    if (isSystem) {
      token.reset(platf::dxgi::retrieve_users_token(false));
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
    }
    return true;
  }

  bool NamedPipeFactory::extract_user_sid_from_token(const safe_token &token, util::c_ptr<TOKEN_USER> &tokenUser, PSID &raw_user_sid) const {
    DWORD len = 0;
    auto tokenHandle = const_cast<HANDLE>(token.get());
    GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      BOOST_LOG(error) << "GetTokenInformation (size query) failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }

    auto tokenBuffer = std::make_unique<uint8_t[]>(len);
    tokenUser.reset(reinterpret_cast<TOKEN_USER *>(tokenBuffer.release()));

    if (!tokenUser || !GetTokenInformation(tokenHandle, TokenUser, tokenUser.get(), len, &len)) {
      BOOST_LOG(error) << "GetTokenInformation (fetch) failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }

    raw_user_sid = tokenUser->User.Sid;
    if (!IsValidSid(raw_user_sid)) {
      BOOST_LOG(error) << "Invalid user SID in create_security_descriptor";
      return false;
    }

    return true;
  }

  bool NamedPipeFactory::create_system_sid(safe_sid &system_sid) const {
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID raw_system_sid = nullptr;
    if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &raw_system_sid)) {
      BOOST_LOG(error) << "AllocateAndInitializeSid failed in create_security_descriptor, error=" << GetLastError();
      return false;
    }
    system_sid.reset(raw_system_sid);

    if (!IsValidSid(system_sid.get())) {
      BOOST_LOG(error) << "Invalid system SID in create_security_descriptor";
      return false;
    }

    return true;
  }

  bool NamedPipeFactory::build_access_control_list(BOOL isSystem, SECURITY_DESCRIPTOR &desc, PSID raw_user_sid, PSID system_sid, PACL *out_pacl) const {
    std::vector<EXPLICIT_ACCESS> eaList;
    if (isSystem) {
      EXPLICIT_ACCESS eaSys {};
      eaSys.grfAccessPermissions = GENERIC_ALL;
      eaSys.grfAccessMode = SET_ACCESS;
      eaSys.grfInheritance = NO_INHERITANCE;
      eaSys.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      eaSys.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
      eaSys.Trustee.ptstrName = (LPTSTR) system_sid;
      eaList.push_back(eaSys);

      EXPLICIT_ACCESS eaUser = eaSys;
      eaUser.Trustee.TrusteeType = TRUSTEE_IS_USER;
      eaUser.Trustee.ptstrName = (LPTSTR) raw_user_sid;
      eaList.push_back(eaUser);
    }

    if (!eaList.empty() && !init_sd_with_explicit_aces(desc, eaList, out_pacl)) {
      BOOST_LOG(error) << "init_sd_with_explicit_aces failed in create_security_descriptor";
      return false;
    }
    return true;
  }

  std::unique_ptr<INamedPipe> NamedPipeFactory::create_server(const std::string &pipeName) {
    auto wPipeBase = utf8_to_wide(pipeName);
    std::wstring fullPipeName = (wPipeBase.find(LR"(\\.\pipe\)") == 0) ? wPipeBase : LR"(\\.\pipe\)" + wPipeBase;

    SECURITY_ATTRIBUTES *pSecAttr = nullptr;
    SECURITY_ATTRIBUTES secAttr {};
    SECURITY_DESCRIPTOR secDesc {};
    PACL rawDacl = nullptr;

    auto fg = util::fail_guard([&rawDacl]() {
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
    return pipeObj;
  }

  std::unique_ptr<INamedPipe> NamedPipeFactory::create_client(const std::string &pipeName) {
    auto wPipeBase = utf8_to_wide(pipeName);
    std::wstring fullPipeName = (wPipeBase.find(LR"(\\.\pipe\)") == 0) ? wPipeBase : LR"(\\.\pipe\)" + wPipeBase;

    safe_handle hPipe = create_client_pipe(fullPipeName);
    if (!hPipe) {
      DWORD err = GetLastError();
      BOOST_LOG(error) << "CreateFileW failed (" << err << ")";
      return nullptr;
    }

    auto pipeObj = std::make_unique<WinPipe>(hPipe.release(), false);
    return pipeObj;
  }

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
          WaitNamedPipeW(fullPipeName.c_str(), 250);
          continue;
        }
        if (err == ERROR_FILE_NOT_FOUND) {
          // Server hasn't created the pipe yet – short back-off
          Sleep(50);
          continue;
        }
        BOOST_LOG(error) << "CreateFileW failed (" << err << ")";
        return safe_handle {};
      }
    }
    return hPipe;
  }

  AnonymousPipeFactory::AnonymousPipeFactory() = default;

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::create_server(const std::string &pipeName) {
    auto first_pipe = _pipe_factory->create_server(pipeName);
    if (!first_pipe) {
      return nullptr;
    }
    return handshake_server(std::move(first_pipe));
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::create_client(const std::string &pipeName) {
    auto first_pipe = _pipe_factory->create_client(pipeName);
    if (!first_pipe) {
      return nullptr;
    }
    return handshake_client(std::move(first_pipe));
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::handshake_server(std::unique_ptr<INamedPipe> pipe) {
    std::string pipe_name = generate_guid();

    if (!send_handshake_message(pipe, pipe_name)) {
      return nullptr;
    }

    if (!wait_for_handshake_ack(pipe)) {
      return nullptr;
    }

    auto dataPipe = _pipe_factory->create_server(pipe_name);
    if (dataPipe) {
      dataPipe->wait_for_client_connection(0);
    }

    pipe->disconnect();
    return dataPipe;
  }

  bool AnonymousPipeFactory::send_handshake_message(std::unique_ptr<INamedPipe> &pipe, const std::string &pipe_name) const {
    std::wstring wpipe_name = utf8_to_wide(pipe_name);

    AnonConnectMsg message {};
    wcsncpy_s(message.pipe_name, wpipe_name.c_str(), _TRUNCATE);

    auto bytes = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(&message),
      sizeof(message)
    );

    pipe->wait_for_client_connection(3000);

    if (!pipe->is_connected()) {
      BOOST_LOG(error) << "Client did not connect to pipe instance within the specified timeout. Disconnecting server pipe.";
      pipe->disconnect();
      return false;
    }

    if (!pipe->send(bytes, 5000)) {
      BOOST_LOG(error) << "Failed to send handshake message to client";
      pipe->disconnect();
      return false;
    }

    return true;
  }

  bool AnonymousPipeFactory::wait_for_handshake_ack(std::unique_ptr<INamedPipe> &pipe) const {
    using enum platf::dxgi::PipeResult;
    std::array<uint8_t, 16> ack_buffer;  // Small buffer for ACK message
    bool ack_ok = false;
    auto t0 = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3) && !ack_ok) {
      size_t bytes_read = 0;
      if (PipeResult result = pipe->receive(ack_buffer, bytes_read, 1000); result == Success) {
        if (bytes_read == 1 && ack_buffer[0] == ACK_MSG) {
          ack_ok = true;
        } else if (bytes_read > 0) {
          BOOST_LOG(warning) << "Received unexpected data during ACK wait, size=" << bytes_read;
        }
      } else if (result == BrokenPipe || result == Error || result == Disconnected) {
        BOOST_LOG(error) << "Pipe error during handshake ACK wait";
        break;
      }
      if (!ack_ok) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    if (!ack_ok) {
      BOOST_LOG(error) << "Handshake ACK not received within timeout - aborting";
      pipe->disconnect();
      return false;
    }

    return true;
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::handshake_client(std::unique_ptr<INamedPipe> pipe) {
    AnonConnectMsg msg {};

    if (!receive_handshake_message(pipe, msg)) {
      return nullptr;
    }

    if (!send_handshake_ack(pipe)) {
      return nullptr;
    }

    std::wstring wpipeName(msg.pipe_name);
    std::string pipeNameStr = wide_to_utf8(wpipeName);
    pipe->disconnect();

    return connect_to_data_pipe(pipeNameStr);
  }

  bool AnonymousPipeFactory::receive_handshake_message(std::unique_ptr<INamedPipe> &pipe, AnonConnectMsg &msg) const {
    using enum platf::dxgi::PipeResult;
    std::array<uint8_t, 256> buffer;  // Buffer for handshake message
    auto start = std::chrono::steady_clock::now();
    bool received = false;
    bool error_occurred = false;
    size_t bytes_read = 0;

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3) && !received && !error_occurred) {
      if (PipeResult result = pipe->receive(buffer, bytes_read, 500); result == Success) {
        if (bytes_read > 0) {
          received = true;
        }
      } else if (result == BrokenPipe || result == Error || result == Disconnected) {
        BOOST_LOG(error) << "Pipe error during handshake message receive";
        error_occurred = true;
      }
      if (!received && !error_occurred) {
        if (bytes_read == 0) {
          BOOST_LOG(warning) << "Received 0 bytes during handshake - server may have closed pipe";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }

    if (!received) {
      BOOST_LOG(error) << "Did not receive handshake message in time. Disconnecting client.";
      pipe->disconnect();
      return false;
    }

    if (bytes_read < sizeof(AnonConnectMsg)) {
      BOOST_LOG(error) << "Received incomplete handshake message (size=" << bytes_read
                       << ", expected=" << sizeof(AnonConnectMsg) << "). Disconnecting client.";
      pipe->disconnect();
      return false;
    }

    std::memcpy(&msg, buffer.data(), sizeof(AnonConnectMsg));
    return true;
  }

  bool AnonymousPipeFactory::send_handshake_ack(std::unique_ptr<INamedPipe> &pipe) const {
    uint8_t ack = ACK_MSG;
    if (!pipe->send(std::span<const uint8_t>(&ack, 1), 5000)) {
      BOOST_LOG(error) << "Failed to send handshake ACK to server";
      pipe->disconnect();
      return false;
    }

    return true;
  }

  std::unique_ptr<INamedPipe> AnonymousPipeFactory::connect_to_data_pipe(const std::string &pipeNameStr) {
    std::unique_ptr<INamedPipe> data_pipe = nullptr;
    auto retry_start = std::chrono::steady_clock::now();
    const auto retry_timeout = std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() - retry_start < retry_timeout) {
      data_pipe = _pipe_factory->create_client(pipeNameStr);
      if (data_pipe) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!data_pipe) {
      BOOST_LOG(error) << "Failed to connect to data pipe after retries";
      return nullptr;
    }

    return data_pipe;
  }

  WinPipe::WinPipe(HANDLE pipe, bool isServer):
      _pipe(pipe),
      _is_server(isServer) {
    if (!_is_server && _pipe != INVALID_HANDLE_VALUE) {
      _connected.store(true, std::memory_order_release);
    }
  }

  WinPipe::~WinPipe() {
    try {
      WinPipe::disconnect();
    } catch (const std::exception &ex) {
      BOOST_LOG(error) << "Exception in WinPipe destructor: " << ex.what();
    } catch (...) {
      BOOST_LOG(error) << "Unknown exception in WinPipe destructor.";
    }
  }

  bool WinPipe::send(std::span<const uint8_t> bytes, int timeout_ms) {
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
      return handle_send_error(ctx, timeout_ms, bytesWritten);
    }

    if (bytesWritten != bytes.size()) {
      BOOST_LOG(error) << "WriteFile wrote " << bytesWritten << " bytes, expected " << bytes.size();
      return false;
    }
    return true;
  }

  bool WinPipe::handle_send_error(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      return handle_pending_send_operation(ctx, timeout_ms, bytesWritten);
    } else {
      BOOST_LOG(error) << "WriteFile failed (" << err << ") in WinPipe::send";
      return false;
    }
  }

  bool WinPipe::handle_pending_send_operation(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten) {
    DWORD waitResult = WaitForSingleObject(ctx->event(), timeout_ms);

    if (waitResult == WAIT_OBJECT_0) {
      if (!GetOverlappedResult(_pipe, ctx->get(), &bytesWritten, FALSE)) {
        DWORD err = GetLastError();
        if (err != ERROR_OPERATION_ABORTED) {
          BOOST_LOG(error) << "GetOverlappedResult failed in send, error=" << err;
        }
        return false;
      }
      return true;
    } else if (waitResult == WAIT_TIMEOUT) {
      BOOST_LOG(warning) << "Send operation timed out after " << timeout_ms << "ms";
      CancelIoEx(_pipe, ctx->get());
      DWORD transferred = 0;
      GetOverlappedResult(_pipe, ctx->get(), &transferred, TRUE);
      return false;
    } else {
      BOOST_LOG(error) << "WaitForSingleObject failed in send, result=" << waitResult << ", error=" << GetLastError();
      return false;
    }
  }

  PipeResult WinPipe::receive(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) {
    bytesRead = 0;
    if (!_connected.load(std::memory_order_acquire) || _pipe == INVALID_HANDLE_VALUE) {
      return PipeResult::Disconnected;
    }

    auto ctx = std::make_unique<io_context>();
    if (!ctx->is_valid()) {
      BOOST_LOG(error) << "Failed to create I/O context for receive operation, error=" << GetLastError();
      return PipeResult::Error;
    }

    DWORD bytesReadWin = 0;
    BOOL result = ReadFile(_pipe, dst.data(), static_cast<DWORD>(dst.size()), &bytesReadWin, ctx->get());

    if (result) {
      bytesRead = static_cast<size_t>(bytesReadWin);
      return PipeResult::Success;
    } else {
      return handle_receive_error(ctx, timeout_ms, dst, bytesRead);
    }
  }

  PipeResult WinPipe::receive_latest(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) {
    PipeResult result = receive(dst, bytesRead, timeout_ms);
    if (result != PipeResult::Success) {
      return result;
    }

    size_t lastBytesRead = bytesRead;
    PipeResult lastResult = result;
    while (true) {
      size_t tempBytesRead = 0;
      PipeResult next = receive(dst, tempBytesRead, 0);
      if (next == PipeResult::Success) {
        lastBytesRead = tempBytesRead;
        lastResult = next;
      } else if (next == PipeResult::Timeout) {
        break;
      } else {
        // If we get an error, broken pipe, or disconnected, return immediately
        return next;
      }
    }
    bytesRead = lastBytesRead;
    return lastResult;
  }

  PipeResult WinPipe::handle_receive_error(std::unique_ptr<io_context> &ctx, int timeout_ms, std::span<uint8_t> dst, size_t &bytesRead) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      return handle_pending_receive_operation(ctx, timeout_ms, dst, bytesRead);
    } else if (err == ERROR_BROKEN_PIPE) {
      BOOST_LOG(warning) << "Pipe broken during ReadFile (ERROR_BROKEN_PIPE)";
      return PipeResult::BrokenPipe;
    } else {
      BOOST_LOG(error) << "ReadFile failed in receive, error=" << err;
      return PipeResult::Error;
    }
  }

  PipeResult WinPipe::handle_pending_receive_operation(std::unique_ptr<io_context> &ctx, int timeout_ms, std::span<uint8_t> dst, size_t &bytesRead) {
    using enum platf::dxgi::PipeResult;
    DWORD waitResult = WaitForSingleObject(ctx->event(), timeout_ms);
    DWORD bytesReadWin = 0;

    if (waitResult == WAIT_OBJECT_0) {
      if (GetOverlappedResult(_pipe, ctx->get(), &bytesReadWin, FALSE)) {
        bytesRead = static_cast<size_t>(bytesReadWin);
        return Success;
      } else {
        DWORD overlappedErr = GetLastError();
        if (overlappedErr == ERROR_BROKEN_PIPE) {
          BOOST_LOG(warning) << "IPC between Sunshine was severed, did the capture process crash?";
          return BrokenPipe;
        }
        if (overlappedErr == ERROR_OPERATION_ABORTED) {
          return Disconnected;
        }
        BOOST_LOG(error) << "GetOverlappedResult failed in receive, error=" << overlappedErr;
        return Error;
      }
    } else if (waitResult == WAIT_TIMEOUT) {
      CancelIoEx(_pipe, ctx->get());
      DWORD transferred = 0;
      GetOverlappedResult(_pipe, ctx->get(), &transferred, TRUE);
      return Timeout;
    } else {
      BOOST_LOG(error) << "WinPipe::receive() wait failed, result=" << waitResult << ", error=" << GetLastError();
      return Error;
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
      }
      CloseHandle(_pipe);
      _pipe = INVALID_HANDLE_VALUE;
    }
    _connected.store(false, std::memory_order_release);
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

    if (BOOL result = ConnectNamedPipe(_pipe, ctx->get()); result) {
      _connected = true;
      return;
    }

    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
      // Client already connected
      _connected = true;
      return;
    }

    if (err == ERROR_IO_PENDING) {
      handle_pending_connection(ctx, milliseconds);
      return;
    }

    BOOST_LOG(error) << "ConnectNamedPipe failed, error=" << err;
  }

  void WinPipe::handle_pending_connection(std::unique_ptr<io_context> &ctx, int milliseconds) {
    // Wait for the connection to complete
    DWORD waitResult = WaitForSingleObject(ctx->event(), milliseconds > 0 ? milliseconds : 5000);  // Use param or default 5s
    if (waitResult == WAIT_OBJECT_0) {
      DWORD transferred = 0;
      if (GetOverlappedResult(_pipe, ctx->get(), &transferred, FALSE)) {
        _connected = true;
      } else {
        DWORD err = GetLastError();
        if (err != ERROR_OPERATION_ABORTED) {
          BOOST_LOG(error) << "GetOverlappedResult failed in connect, error=" << err;
        }
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
  }

  bool WinPipe::is_connected() {
    return _connected.load(std::memory_order_acquire);
  }

  void WinPipe::flush_buffers() {
    if (_pipe != INVALID_HANDLE_VALUE) {
      FlushFileBuffers(_pipe);
    }
  }

  AsyncNamedPipe::AsyncNamedPipe(std::unique_ptr<INamedPipe> pipe):
      _pipe(std::move(pipe)) {
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
    _worker = std::jthread(&AsyncNamedPipe::worker_thread, this);
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

  void AsyncNamedPipe::send(std::span<const uint8_t> message) {
    safe_execute_operation("send", [this, message]() {
      if (_pipe && _pipe->is_connected() && !_pipe->send(message, 5000)) {  // 5 second timeout for async sends
        BOOST_LOG(warning) << "Failed to send message through AsyncNamedPipe (timeout or error)";
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
    using enum platf::dxgi::PipeResult;

    // No need to add a sleep here: _pipe->receive() blocks until data arrives or times out, so messages are delivered to callbacks as soon as they are available.
    while (_running.load(std::memory_order_acquire)) {
      size_t bytesRead = 0;
      PipeResult res = _pipe->receive(_buffer, bytesRead, 1000);

      // Fast cancel – bail out even before decoding res
      if (!_running.load(std::memory_order_acquire)) {
        break;
      }

      switch (res) {
        case Success:
          {
            if (bytesRead == 0) {  // remote closed
              return;
            }
            // Create span from only the valid portion of the buffer
            std::span<const uint8_t> message(_buffer.data(), bytesRead);
            process_message(message);
            break;  // keep looping
          }

        case Timeout:
          break;  // nothing to do

        case BrokenPipe:
          safe_execute_operation("brokenPipe callback", _onBrokenPipe);
          return;  // terminate

        case Error:
        case Disconnected:
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

  void AsyncNamedPipe::process_message(std::span<const uint8_t> bytes) const {
    if (!_onMessage) {
      return;
    }

    safe_execute_operation("message callback", [this, bytes]() {
      _onMessage(bytes);
    });
  }
}  // namespace platf::dxgi
