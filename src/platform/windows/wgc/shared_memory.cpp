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

namespace {
  // RAII wrappers for Windows security objects
  using safe_token = util::safe_ptr_v2<void, BOOL, &CloseHandle>;
  using safe_sid = util::safe_ptr_v2<void, PVOID, &FreeSid>;
  using safe_local_mem = util::safe_ptr_v2<void, HLOCAL, &LocalFree>;
  using safe_handle = platf::dxgi::safe_handle;
  
  // Specialized wrapper for DACL since it needs to be cast to PACL
  struct safe_dacl {
    PACL dacl = nullptr;
    
    safe_dacl() = default;
    explicit safe_dacl(PACL p) : dacl(p) {}
    
    ~safe_dacl() {
      if (dacl) {
        LocalFree(dacl);
      }
    }
    
    // Move constructor
    safe_dacl(safe_dacl&& other) noexcept : dacl(other.dacl) {
      other.dacl = nullptr;
    }
    
    // Move assignment
    safe_dacl& operator=(safe_dacl&& other) noexcept {
      if (this != &other) {
        if (dacl) {
          LocalFree(dacl);
        }
        dacl = other.dacl;
        other.dacl = nullptr;
      }
      return *this;
    }
    
    // Disable copy
    safe_dacl(const safe_dacl&) = delete;
    safe_dacl& operator=(const safe_dacl&) = delete;
    
    void reset(PACL p = nullptr) {
      if (dacl) {
        LocalFree(dacl);
      }
      dacl = p;
    }
    
    PACL get() const { return dacl; }
    PACL release() {
      PACL tmp = dacl;
      dacl = nullptr;
      return tmp;
    }
    
    explicit operator bool() const { return dacl != nullptr; }
  };
}

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

// --- SharedSessionManager Implementation ---
SecuredPipeCoordinator::SecuredPipeCoordinator(IAsyncPipeFactory *pipeFactory):
    _pipeFactory(pipeFactory) {}

std::unique_ptr<IAsyncPipe> SecuredPipeCoordinator::prepare_client(std::unique_ptr<IAsyncPipe> pipe) {
  SecureClientMessage msg {};  // Zero-initialize

  std::vector<uint8_t> bytes;
  auto start = std::chrono::steady_clock::now();
  bool received = false;

  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
    pipe->receive(bytes, true); // Use blocking receive to wait for handshake
    if (!bytes.empty()) {
      received = true;
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

  if (bytes.size() < sizeof(SecureClientMessage)) {
    BOOST_LOG(error) << "Received incomplete handshake message (size=" << bytes.size() 
                     << ", expected=" << sizeof(SecureClientMessage) << "). Disconnecting client.";
    pipe->disconnect();
    return nullptr;
  }

  std::memcpy(&msg, bytes.data(), sizeof(SecureClientMessage));

  // Send ACK (1 byte) with blocking to ensure delivery
  std::vector<uint8_t> ack(1, 0xA5);
  BOOST_LOG(info) << "Sending handshake ACK to server";
  pipe->send(ack, true);

  // Convert wide string to string using proper conversion
  std::wstring wpipeNasme(msg.pipe_name);
  std::wstring weventName(msg.event_name);
  std::string pipeNameStr = wide_to_utf8(wpipeNasme);
  std::string eventNameStr = wide_to_utf8(weventName);

  // Disconnect control pipe only after ACK is sent
  pipe->disconnect();

  // Retry logic for opening the data pipe
  std::unique_ptr<IAsyncPipe> data_pipe = nullptr;
  auto retry_start = std::chrono::steady_clock::now();
  const auto retry_timeout = std::chrono::seconds(5);

  while (std::chrono::steady_clock::now() - retry_start < retry_timeout) {
    // Use secured=true to match the server side
    data_pipe = _pipeFactory->create(pipeNameStr, eventNameStr, false, true);
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

std::unique_ptr<IAsyncPipe> SecuredPipeCoordinator::prepare_server(std::unique_ptr<IAsyncPipe> pipe) {
  std::string pipe_name = generateGuid();
  std::string event_name = generateGuid();

  std::wstring wpipe_name = utf8_to_wide(pipe_name);
  std::wstring wevent_name = utf8_to_wide(event_name);

  SecureClientMessage message {};
  wcsncpy_s(message.pipe_name, wpipe_name.c_str(), _TRUNCATE);
  wcsncpy_s(message.event_name, wevent_name.c_str(), _TRUNCATE);

  std::vector<uint8_t> bytes(sizeof(SecureClientMessage));
  std::memcpy(bytes.data(), &message, sizeof(SecureClientMessage));

  pipe->wait_for_client_connection(3000);

  if(!pipe->is_connected()){
    BOOST_LOG(error) << "Client did not connect to pipe instance within the specified timeout. Disconnecting server pipe.";
    pipe->disconnect();
    return nullptr;
  }
  BOOST_LOG(info) << "Sending handshake message to client with pipe_name='" << pipe_name << "' and event_name='" << event_name << "'";
  pipe->send(bytes, true);  // Block to ensure WriteFile completes

  // Wait for ACK from client before disconnecting
  std::vector<uint8_t> ack;
  bool ack_ok = false;
  auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
    pipe->receive(ack, true);  // Blocking receive to wait for ACK
    if (ack.size() == 1 && ack[0] == 0xA5) {
      ack_ok = true;
      BOOST_LOG(info) << "Received handshake ACK from client";
      break;
    }
    if (!ack.empty()) {
      BOOST_LOG(warning) << "Received unexpected data during ACK wait, size=" << ack.size();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  if (!ack_ok) {
    BOOST_LOG(error) << "Handshake ACK not received within timeout - aborting";
    pipe->disconnect();
    return nullptr;
  }

  pipe->disconnect();

  auto dataPipe = _pipeFactory->create(pipe_name, event_name, true, true);
  // Ensure the server side waits for a client
  if (dataPipe) {
    dataPipe->wait_for_client_connection(0);  // 0 = immediate IF connected, otherwise overlapped wait
  }
  return dataPipe;
}

std::string SecuredPipeCoordinator::generateGuid() const {
  GUID guid;
  if (CoCreateGuid(&guid) != S_OK) {
    return {};
  }

  std::array<WCHAR, 39> guidStr{};  // "{...}" format, 38 chars + null
  if (StringFromGUID2(guid, guidStr.data(), 39) == 0) {
    return {};
  }

  // Convert WCHAR to std::string using proper conversion
  std::wstring wstr(guidStr.data());
  return wide_to_utf8(wstr);
}

bool AsyncPipeFactory::create_security_descriptor(SECURITY_DESCRIPTOR &desc) const {
  safe_token token;
  util::c_ptr<TOKEN_USER> tokenUser;
  safe_sid user_sid;
  safe_sid system_sid;
  safe_dacl pDacl;

  BOOL isSystem = platf::wgc::is_running_as_system();
  
  BOOST_LOG(info) << "create_security_descriptor: isSystem=" << isSystem;

  if (isSystem) {
    token.reset(platf::wgc::retrieve_users_token(false));
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
  tokenUser.reset(reinterpret_cast<TOKEN_USER*>(tokenBuffer.release()));
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

  // Build DACL: always allow SYSTEM and user full access
  EXPLICIT_ACCESS ea[2] = {};
  int aceCount = 0;
  
  // Always add SYSTEM SID to ACL for consistent access
  if (system_sid) {
    ea[aceCount].grfAccessPermissions = GENERIC_ALL;
    ea[aceCount].grfAccessMode = SET_ACCESS;
    ea[aceCount].grfInheritance = NO_INHERITANCE;
    ea[aceCount].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[aceCount].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[aceCount].Trustee.ptstrName = (LPTSTR) system_sid.get();
    BOOST_LOG(info) << "create_security_descriptor: Added SYSTEM SID to ACL at index " << aceCount;
    aceCount++;
  }
  
  // Always add user SID to ACL if we have one
  // When running as SYSTEM, this is the impersonated user who needs access
  // When not running as SYSTEM, this is the current user
  if (raw_user_sid) {
    ea[aceCount].grfAccessPermissions = GENERIC_ALL;
    ea[aceCount].grfAccessMode = SET_ACCESS;
    ea[aceCount].grfInheritance = NO_INHERITANCE;
    ea[aceCount].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[aceCount].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[aceCount].Trustee.ptstrName = (LPTSTR) raw_user_sid;
    BOOST_LOG(info) << "create_security_descriptor: Added user SID to ACL at index " << aceCount;
    aceCount++;
  }
  
  BOOST_LOG(info) << "create_security_descriptor: Total ACE count=" << aceCount;
  if (aceCount > 0) {
    PACL raw_dacl = nullptr;
    DWORD err = SetEntriesInAcl(aceCount, ea, nullptr, &raw_dacl);
    if (err == ERROR_SUCCESS) {
      pDacl.reset(raw_dacl);
      if (!SetSecurityDescriptorDacl(&desc, TRUE, pDacl.get(), FALSE)) {
        BOOST_LOG(error) << "SetSecurityDescriptorDacl failed in create_security_descriptor, error=" << GetLastError();
        return false;
      }
    } else {
      BOOST_LOG(error) << "SetEntriesInAcl failed in create_security_descriptor, error=" << err;
      return false;
    }
  }

  // Transfer ownership of pDacl to the security descriptor - don't let RAII clean it up
  pDacl.release();
  return true;  // Success
}

bool AsyncPipeFactory::create_security_descriptor_for_target_process(SECURITY_DESCRIPTOR &desc, DWORD target_pid) const {
  safe_handle target_process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, target_pid));
  if (!target_process) {
    DWORD err = GetLastError();
    BOOST_LOG(error) << "Failed to open target process for security descriptor, pid=" << target_pid << ", error=" << err;
    return false;
  }

  HANDLE raw_target_token = nullptr;
  if (!OpenProcessToken(target_process.get(), TOKEN_QUERY, &raw_target_token)) {
    DWORD err = GetLastError();
    BOOST_LOG(error) << "Failed to open target process token, pid=" << target_pid << ", error=" << err;
    return false;
  }
  safe_token target_token(raw_target_token);

  util::c_ptr<TOKEN_USER> tokenUser;
  safe_sid system_sid;
  safe_dacl pDacl;

  BOOL isSystem = platf::wgc::is_running_as_system();
  
  BOOST_LOG(info) << "create_security_descriptor_for_target_process: isSystem=" << isSystem << ", target_pid=" << target_pid;

  // Extract user SID from target process token
  DWORD len = 0;
  GetTokenInformation(target_token.get(), TokenUser, nullptr, 0, &len);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    BOOST_LOG(error) << "GetTokenInformation (size query) failed for target process, error=" << GetLastError();
    return false;
  }

  auto tokenBuffer = std::make_unique<uint8_t[]>(len);
  tokenUser.reset(reinterpret_cast<TOKEN_USER*>(tokenBuffer.release()));
  if (!tokenUser || !GetTokenInformation(target_token.get(), TokenUser, tokenUser.get(), len, &len)) {
    BOOST_LOG(error) << "GetTokenInformation (fetch) failed for target process, error=" << GetLastError();
    return false;
  }
  PSID raw_user_sid = tokenUser->User.Sid;

  // Validate the user SID
  if (!IsValidSid(raw_user_sid)) {
    BOOST_LOG(error) << "Invalid user SID for target process";
    return false;
  }
  
  // Log the user SID for debugging
  if (LPWSTR targetSidString = nullptr; ConvertSidToStringSidW(raw_user_sid, &targetSidString)) {
    std::wstring wsid(targetSidString);
    BOOST_LOG(info) << "create_security_descriptor_for_target_process: Target User SID=" << wide_to_utf8(wsid);
    LocalFree(targetSidString);
  }

  // Create SYSTEM SID if needed
  if (isSystem) {
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID raw_system_sid = nullptr;
    if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &raw_system_sid)) {
      BOOST_LOG(error) << "AllocateAndInitializeSid failed for target process, error=" << GetLastError();
      return false;
    }
    system_sid.reset(raw_system_sid);

    // Validate the system SID
    if (!IsValidSid(system_sid.get())) {
      BOOST_LOG(error) << "Invalid system SID for target process";
      return false;
    }
    
    // Log the system SID for debugging
    if (LPWSTR systemTargetSidString = nullptr; ConvertSidToStringSidW(system_sid.get(), &systemTargetSidString)) {
      std::wstring wsid(systemTargetSidString);
      BOOST_LOG(info) << "create_security_descriptor_for_target_process: System SID=" << wide_to_utf8(wsid);
      LocalFree(systemTargetSidString);
    }
  }

  // Initialize security descriptor
  if (!InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION)) {
    BOOST_LOG(error) << "InitializeSecurityDescriptor failed for target process, error=" << GetLastError();
    return false;
  }

  // Build DACL: allow SYSTEM (if running as system) and target process user full access
  EXPLICIT_ACCESS ea[2] = {};
  int aceCount = 0;
  
  // When running as SYSTEM, always add SYSTEM SID to ACL
  if (isSystem && system_sid) {
    ea[aceCount].grfAccessPermissions = GENERIC_ALL;
    ea[aceCount].grfAccessMode = SET_ACCESS;
    ea[aceCount].grfInheritance = NO_INHERITANCE;
    ea[aceCount].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[aceCount].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[aceCount].Trustee.ptstrName = (LPTSTR) system_sid.get();
    BOOST_LOG(info) << "create_security_descriptor_for_target_process: Added SYSTEM SID to ACL at index " << aceCount;
    aceCount++;
  }
  
  // Always add target process user SID to ACL
  if (raw_user_sid) {
    ea[aceCount].grfAccessPermissions = GENERIC_ALL;
    ea[aceCount].grfAccessMode = SET_ACCESS;
    ea[aceCount].grfInheritance = NO_INHERITANCE;
    ea[aceCount].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[aceCount].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[aceCount].Trustee.ptstrName = (LPTSTR) raw_user_sid;
    BOOST_LOG(info) << "create_security_descriptor_for_target_process: Added target user SID to ACL at index " << aceCount;
    aceCount++;
  }
  
  BOOST_LOG(info) << "create_security_descriptor_for_target_process: Total ACE count=" << aceCount;
  if (aceCount > 0) {
    PACL raw_dacl = nullptr;
    DWORD err = SetEntriesInAcl(aceCount, ea, nullptr, &raw_dacl);
    if (err == ERROR_SUCCESS) {
      pDacl.reset(raw_dacl);
      if (!SetSecurityDescriptorDacl(&desc, TRUE, pDacl.get(), FALSE)) {
        BOOST_LOG(error) << "SetSecurityDescriptorDacl failed for target process, error=" << GetLastError();
        return false;
      }
    } else {
      BOOST_LOG(error) << "SetEntriesInAcl failed for target process, error=" << err;
      return false;
    }
  }

  // Transfer ownership of pDacl to the security descriptor - don't let RAII clean it up
  pDacl.release();
  return true;  // Success
}

// --- AsyncPipeFactory Implementation ---
std::unique_ptr<IAsyncPipe> AsyncPipeFactory::create(
  const std::string &pipeName,
  const std::string &eventName,
  bool isServer,
  bool isSecured
) {
  BOOST_LOG(info) << "AsyncPipeFactory::create called with pipeName='"
                  << pipeName << "', eventName='" << eventName
                  << "', isServer=" << isServer
                  << ", isSecured=" << isSecured;

  auto wPipeBase = utf8_to_wide(pipeName);
  std::wstring fullPipeName;
  if (wPipeBase.find(LR"(\\.\pipe\)") == 0) {
    // Pipe name already has the full prefix
    fullPipeName = wPipeBase;
  } else {
    // Need to add the prefix
    fullPipeName = LR"(\\.\pipe\)" + wPipeBase;
  }
  std::wstring wEventName = utf8_to_wide(eventName);

  // Build SECURITY_ATTRIBUTES if requested OR if running as SYSTEM
  // When running as SYSTEM, we always need security attributes to allow user processes to access the pipe
  SECURITY_ATTRIBUTES *pSecAttr = nullptr;
  SECURITY_ATTRIBUTES secAttr {};
  SECURITY_DESCRIPTOR secDesc {};
  
  if (const BOOL needsSecurityDescriptor = isSecured || platf::wgc::is_running_as_system(); needsSecurityDescriptor) {
    if (!create_security_descriptor(secDesc)) {
      BOOST_LOG(error) << "Failed to init security descriptor";
      return nullptr;
    }
    secAttr = {sizeof(secAttr), &secDesc, FALSE};
    pSecAttr = &secAttr;
    BOOST_LOG(info) << "Security attributes prepared (isSecured=" << isSecured << ", isSystem=" << platf::wgc::is_running_as_system() << ").";
  }

  // Create event (manual‑reset, non‑signaled)
  // Use the same security attributes as the pipe for consistency
  safe_handle hEvent(CreateEventW(pSecAttr, TRUE, FALSE, wEventName.c_str()));
  if (!hEvent) {
    DWORD err = GetLastError();
    BOOST_LOG(error) << "CreateEventW failed (" << err << ")";
    return nullptr;
  }

  safe_handle hPipe;
  if (isServer) {
    hPipe.reset(CreateNamedPipeW(
      fullPipeName.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      65536,
      65536,
      0,
      pSecAttr
    ));
  } else {
    hPipe = create_client_pipe(fullPipeName);
  }

  if (!hPipe) {
    DWORD err = GetLastError();
    BOOST_LOG(error) << (isServer ? "CreateNamedPipeW" : "CreateFileW")
                     << " failed (" << err << ")";
    return nullptr;
  }

  auto pipeObj = std::make_unique<AsyncPipe>(hPipe.release(), hEvent.release(), isServer);
  BOOST_LOG(info) << "Returning AsyncPipe for '" << pipeName << "'";
  return pipeObj;
}

// Helper function to create client pipe with retry logic
safe_handle AsyncPipeFactory::create_client_pipe(const std::wstring& fullPipeName) const {
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
        return safe_handle{};
      }
    }
  }
  return hPipe;
}

SecuredPipeFactory::SecuredPipeFactory():
    _pipeFactory(std::make_unique<AsyncPipeFactory>()),
    _coordinator(_pipeFactory.get()) {}

std::unique_ptr<IAsyncPipe> SecuredPipeFactory::create(const std::string &pipeName, const std::string &eventName, bool isServer, bool isSecured) {
  std::string pipeNameWithPid = pipeName;
  std::string eventNameWithPid = eventName;
  // Only append _PID for the initial unsecured pipe
  if (!isSecured) {
    DWORD pid = 0;
    if (isServer) {
      pid = GetCurrentProcessId();
    } else {
      pid = platf::wgc::get_parent_process_id();
    }
    pipeNameWithPid = std::format("{}_{}", pipeName, pid);
    eventNameWithPid = std::format("{}_{}", eventName, pid);
  }

  auto first_pipe = _pipeFactory->create(pipeNameWithPid, eventNameWithPid, isServer, isSecured);
  if (!first_pipe) {
    return nullptr;
  }
  if (isServer) {
    return _coordinator.prepare_server(std::move(first_pipe));
  }
  return _coordinator.prepare_client(std::move(first_pipe));
}

// --- AsyncPipe Implementation ---
AsyncPipe::AsyncPipe(HANDLE pipe, HANDLE event, bool isServer):
    _pipe(pipe),
    _event(event),
    _connected(false),
    _isServer(isServer),
    _running(false),
    _readPending(false) {
  // Set _connected = true for client pipes immediately if handle is valid
  if (!_isServer && _pipe != INVALID_HANDLE_VALUE) {
    _connected = true;
    BOOST_LOG(info) << "AsyncPipe (client): Connected immediately after CreateFileW, handle valid.";
  }
  
  // Initialize overlapped structure for persistent read operations
  ZeroMemory(&_readOverlapped, sizeof(_readOverlapped));
  _readOverlapped.hEvent = _event;
}

AsyncPipe::~AsyncPipe() {
  AsyncPipe::disconnect();
}

void AsyncPipe::send(std::vector<uint8_t> bytes) {
  send(bytes, false);
}

void AsyncPipe::send(const std::vector<uint8_t> &bytes, bool block) {
  if (!_connected || _pipe == INVALID_HANDLE_VALUE) {
    return;
  }
  
  OVERLAPPED ovl = {0};
  safe_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); // fresh event for each op
  if (!event) {
    BOOST_LOG(error) << "Failed to create event for send operation, error=" << GetLastError();
    return;
  }
  ovl.hEvent = event.get();
  ResetEvent(ovl.hEvent);
  
  DWORD bytesWritten = 0;
  if (BOOL result = WriteFile(_pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, &ovl); !result) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING && block) {
      BOOST_LOG(info) << "WriteFile is pending, waiting for completion.";
      WaitForSingleObject(ovl.hEvent, INFINITE);
      GetOverlappedResult(_pipe, &ovl, &bytesWritten, FALSE);
    } else {
      BOOST_LOG(error) << "WriteFile failed (" << err << ") in AsyncPipe::send";
      return;           // bail out – don't pretend we sent anything
    }
  } 
  FlushFileBuffers(_pipe);   // guarantees delivery of the handshake payload
}

void AsyncPipe::receive(std::vector<uint8_t> &bytes) {
  receive(bytes, false);
}

void AsyncPipe::receive(std::vector<uint8_t> &bytes, bool block) {
  bytes.clear();
  if (!_connected || _pipe == INVALID_HANDLE_VALUE) {
    return;
  }

  // Check if we have immediate data available from a previous begin_async_read
  if (!_readPending && !_readBuffer.empty()) {
    bytes = std::move(_readBuffer);
    _readBuffer.clear();
    // Close the per-operation event if any - now handled by RAII in begin_async_read
    if (_readOverlapped.hEvent && _readOverlapped.hEvent != _event) {
      CloseHandle(_readOverlapped.hEvent);
      _readOverlapped.hEvent = nullptr;
    }
    return;
  }

  // If we don't have a pending read, start one
  if (!_readPending) {
    begin_async_read();
  }

  if (!_readPending) {
    return; // Failed to start read
  }

  if (block) {
    // Wait for the read to complete with a timeout to allow graceful shutdown
    DWORD waitResult = WaitForSingleObject(_readOverlapped.hEvent, 100); // 100ms timeout
    if (waitResult == WAIT_OBJECT_0) {
      complete_async_read(bytes);
    } else if (waitResult == WAIT_TIMEOUT) {
      // Don't complete the read, just return empty bytes
    } else {
      BOOST_LOG(error) << "AsyncPipe::receive() wait failed, result=" << waitResult << ", error=" << GetLastError();
    }
  } else {
    // Check if read has completed without blocking
    DWORD waitResult = WaitForSingleObject(_readOverlapped.hEvent, 0);
    if (waitResult == WAIT_OBJECT_0) {
      complete_async_read(bytes);
    }
  }
}

void AsyncPipe::begin_async_read() {
  if (_readPending || !_connected || _pipe == INVALID_HANDLE_VALUE) {
    return;
  }

  // Clear any previous buffer data
  _readBuffer.clear();
  
  ZeroMemory(&_readOverlapped, sizeof(_readOverlapped));
  // Create a fresh auto-reset event for each read operation
  _readOverlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset, non-signaled
  if (!_readOverlapped.hEvent) {
    BOOST_LOG(error) << "Failed to create read event, error=" << GetLastError();
    return;
  }

  _readBuffer.resize(4096);
  
  DWORD bytesRead = 0;
  BOOL result = ReadFile(_pipe, _readBuffer.data(), static_cast<DWORD>(_readBuffer.size()), &bytesRead, &_readOverlapped);
  
  if (result) {
    // Read completed immediately
    _readBuffer.resize(bytesRead);
    _readPending = false;
    SetEvent(_readOverlapped.hEvent); // Signal that data is available
  } else {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      _readPending = true;
    } else {
      BOOST_LOG(error) << "ReadFile failed in begin_async_read, error=" << err;
      _readPending = false;
      _readBuffer.clear(); // Clear buffer on error
    }
  }
}

void AsyncPipe::complete_async_read(std::vector<uint8_t> &bytes) {
  if (!_readPending) {
    // Data was available immediately from begin_async_read
    bytes = std::move(_readBuffer);
    _readBuffer.clear();
    // Close the per-operation event - check if it's not the main event
    if (_readOverlapped.hEvent && _readOverlapped.hEvent != _event) {
      CloseHandle(_readOverlapped.hEvent);
      _readOverlapped.hEvent = nullptr;
    }
    return;
  }

  if (DWORD bytesRead = 0; GetOverlappedResult(_pipe, &_readOverlapped, &bytesRead, FALSE)) {
    _readBuffer.resize(bytesRead);
    bytes = std::move(_readBuffer);
    _readBuffer.clear();
    if (bytesRead == 0) {
      BOOST_LOG(info) << "AsyncPipe: Read completed with 0 bytes (remote end closed pipe)";
    }
  } else {
    DWORD err = GetLastError();
    BOOST_LOG(error) << "GetOverlappedResult failed in complete_async_read, error=" << err;
    if (err == ERROR_BROKEN_PIPE) {
      BOOST_LOG(error) << "Pipe was closed by remote end during read operation";
    }
  }
  
  _readPending = false;
  // Close the per-operation event - check if it's not the main event
  if (_readOverlapped.hEvent && _readOverlapped.hEvent != _event) {
    CloseHandle(_readOverlapped.hEvent);
    _readOverlapped.hEvent = nullptr;
  }
  ResetEvent(_event);
}

void AsyncPipe::wait_for_client_connection(int milliseconds) {
  if (_pipe == INVALID_HANDLE_VALUE) {
    return;
  }

  if (_isServer) {
    // For server pipes, use ConnectNamedPipe with proper overlapped I/O
    OVERLAPPED ovl = {0};
    safe_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); // fresh event for connect
    if (!event) {
      BOOST_LOG(error) << "Failed to create event for connection, error=" << GetLastError();
      return;
    }
    ovl.hEvent = event.get();
    ResetEvent(ovl.hEvent);

    connect_server_pipe(ovl, milliseconds);
    // event is automatically cleaned up by RAII
  } else {
    // For client handles created with CreateFileW, the connection already exists
    // _connected is set in constructor
  }
}

void AsyncPipe::connect_server_pipe(OVERLAPPED& ovl, int milliseconds) {
  BOOL result = ConnectNamedPipe(_pipe, &ovl);
  if (result) {
    _connected = true;
    BOOST_LOG(info) << "AsyncPipe (server): Connected after ConnectNamedPipe returned true.";
  } else {
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
      // Client already connected
      _connected = true;
      
      // NEW: flip to the correct read‑mode so the first WriteFile
      // is accepted by the pipe instance
      DWORD dwMode = PIPE_READMODE_BYTE | PIPE_WAIT;
      SetNamedPipeHandleState(_pipe, &dwMode, nullptr, nullptr);
      BOOST_LOG(info) << "AsyncPipe (server): Client pre‑connected, mode set.";
    } else if (err == ERROR_IO_PENDING) {
      // Wait for the connection to complete
      DWORD waitResult = WaitForSingleObject(ovl.hEvent, milliseconds > 0 ? milliseconds : 5000);  // Use param or default 5s
      if (waitResult == WAIT_OBJECT_0) {
        DWORD transferred = 0;
        if (GetOverlappedResult(_pipe, &ovl, &transferred, FALSE)) {
          _connected = true;
          
          // NEW: flip to the correct read‑mode so the first WriteFile
          // is accepted by the pipe instance
          DWORD dwMode = PIPE_READMODE_BYTE | PIPE_WAIT;
          SetNamedPipeHandleState(_pipe, &dwMode, nullptr, nullptr);
          BOOST_LOG(info) << "AsyncPipe (server): Connected after overlapped ConnectNamedPipe completed, mode set.";
        } else {
          BOOST_LOG(error) << "GetOverlappedResult failed in connect, error=" << GetLastError();
        }
      } else {
        BOOST_LOG(error) << "ConnectNamedPipe timeout or wait failed, waitResult=" << waitResult << ", error=" << GetLastError();
      }
    } else {
      BOOST_LOG(error) << "ConnectNamedPipe failed, error=" << err;
    }
  }
}

void AsyncPipe::disconnect() {
  _running = false;
  
  // Cancel any pending I/O operations
  if (_pipe != INVALID_HANDLE_VALUE && _readPending) {
    CancelIo(_pipe);
    _readPending = false;
  }
  
  // Clear the read buffer
  _readBuffer.clear();
  
  // Clean up per-operation event if one is pending (but not the main event)
  if (_readOverlapped.hEvent && _readOverlapped.hEvent != _event) {
    CloseHandle(_readOverlapped.hEvent);
    _readOverlapped.hEvent = nullptr;
  }
  
  if (_pipe != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(_pipe);
    if (_isServer) {
      DisconnectNamedPipe(_pipe);
      BOOST_LOG(info) << "AsyncPipe (server): Disconnected via DisconnectNamedPipe.";
    } else {
      BOOST_LOG(info) << "AsyncPipe (client): Disconnected (no DisconnectNamedPipe).";
    }
    CloseHandle(_pipe);
    _pipe = INVALID_HANDLE_VALUE;
  }
  if (_event) {
    CloseHandle(_event);
    _event = nullptr;
  }
  _connected = false;
  BOOST_LOG(info) << "AsyncPipe: Connection state set to false (disconnected).";
}

bool AsyncPipe::is_connected() {
  return _connected;
}

// --- AsyncNamedPipe Implementation ---
AsyncNamedPipe::AsyncNamedPipe(std::unique_ptr<IAsyncPipe> pipe):
    _pipe(std::move(pipe)),
    _running(false) {}

AsyncNamedPipe::~AsyncNamedPipe() {
  stop();
}

bool AsyncNamedPipe::start(const MessageCallback& onMessage, const ErrorCallback& onError) {
  if (_running) {
    return false; // Already running
  }
  
  if (!_pipe) {
    if (onError) {
      onError("No pipe available - failed to create pipe");
    }
    return false;
  }
  
  _onMessage = onMessage;
  _onError = onError;
  
  _running = true;
  _worker = std::thread(&AsyncNamedPipe::workerThread, this);
  return true;
}

void AsyncNamedPipe::stop() {
  _running = false;
  
  // Cancel any pending I/O operations to unblock the worker thread
  if (_pipe) {
    _pipe->disconnect();
  }
  
  if (_worker.joinable()) {
    _worker.join();
  }
}

void AsyncNamedPipe::asyncSend(const std::vector<uint8_t> &message) {
  if (_pipe && _pipe->is_connected()) {
    _pipe->send(message);
  }
}

void AsyncNamedPipe::wait_for_client_connection(int milliseconds) {
  _pipe->wait_for_client_connection(milliseconds);
}

bool AsyncNamedPipe::isConnected() const {
  return _pipe && _pipe->is_connected();
}

void AsyncNamedPipe::workerThread() {
  try {
    if (!establishConnection()) {
      return;
    }

    // Start the first async read
    if (_pipe && _pipe->is_connected()) {
      _pipe->begin_async_read();
    }

    while (_running && _pipe && _pipe->is_connected()) {
      std::vector<uint8_t> bytes;
      _pipe->receive(bytes, true); // This will now wait for the overlapped operation to complete

      if (!_running) {
        return;
      }

      if (!bytes.empty()) {
        processMessage(bytes);
      }

      // Start the next async read
      if (_running && _pipe && _pipe->is_connected()) {
        _pipe->begin_async_read();
      }
    }
  } catch (const std::exception& e) {
    handleWorkerException(e);
  } catch (...) {
    handleWorkerUnknownException();
  }
}

bool AsyncNamedPipe::establishConnection() {
  // For server pipes, we need to wait for a client connection first
  if (!_pipe || _pipe->is_connected()) {
    return true;
  }
  
  _pipe->wait_for_client_connection(5000); // Wait up to 5 seconds for connection
  if (!_pipe->is_connected() && _onError) {
    _onError("Failed to establish connection within timeout");
    return false;
  }
  return _pipe->is_connected();
}

void AsyncNamedPipe::processMessage(const std::vector<uint8_t>& bytes) const {
  if (!_onMessage) {
    return;
  }
  
  try {
    _onMessage(bytes);
  } catch (const std::exception& e) {
    BOOST_LOG(error) << "AsyncNamedPipe: Exception in message callback: " << e.what();
    // Continue processing despite callback exception
  } catch (...) {
    BOOST_LOG(error) << "AsyncNamedPipe: Unknown exception in message callback";
    // Continue processing despite callback exception
  }
}

void AsyncNamedPipe::handleWorkerException(const std::exception& e) const {
  BOOST_LOG(error) << "AsyncNamedPipe worker thread exception: " << e.what();
  if (!_onError) {
    return;
  }
  
  try {
    _onError(std::string("Worker thread exception: ") + e.what());
  } catch (...) {
    BOOST_LOG(error) << "AsyncNamedPipe: Exception in error callback";
  }
}

void AsyncNamedPipe::handleWorkerUnknownException() const {
  BOOST_LOG(error) << "AsyncNamedPipe worker thread unknown exception";
  if (!_onError) {
    return;
  }
  
  try {
    _onError("Worker thread unknown exception");
  } catch (...) {
    BOOST_LOG(error) << "AsyncNamedPipe: Exception in error callback";
  }
}
