#pragma once

#include "src/logging.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

constexpr uint8_t HEARTBEAT_MSG = 0x01;
constexpr uint8_t FRAME_READY_MSG = 0x03;
constexpr uint8_t SECURE_DESKTOP_MSG = 0x02;
constexpr uint8_t ACK_MSG = 0xA5;

// Forward declaration for RAII wrapper
namespace platf::dxgi {
  struct safe_handle;
}

class INamedPipe {
public:
  /**
   * @brief Virtual destructor for safe polymorphic deletion.
   */
  virtual ~INamedPipe() = default;

  /**
   * @brief Sends a message through the pipe with timeout.
   * @param bytes The message to send as a vector of bytes.
   * @param timeout_ms Maximum time to wait for send completion, in milliseconds.
   * @return True if sent successfully, false on timeout or error.
   */
  virtual bool send(std::vector<uint8_t> bytes, int timeout_ms) = 0;

  /**
   * @brief Receives a message from the pipe with timeout.
   * @param bytes The received message will be stored in this vector.
   * @param timeout_ms Maximum time to wait for receive completion, in milliseconds.
   * @return True if received successfully, false on timeout or error.
   */
  virtual bool receive(std::vector<uint8_t> &bytes, int timeout_ms) = 0;

  /**
   * @brief Connect to the pipe and verify that the client has connected.
   * @param milliseconds Maximum time to wait for connection, in milliseconds.
   */
  virtual void wait_for_client_connection(int milliseconds) = 0;

  /**
   * @brief Disconnects the pipe and releases any associated resources.
   */
  virtual void disconnect() = 0;

  /**
   * @brief Checks if the pipe is currently connected.
   * @return True if connected, false otherwise.
   */
  virtual bool is_connected() = 0;
};

class AsyncNamedPipe {
public:
  using MessageCallback = std::function<void(const std::vector<uint8_t> &)>;
  using ErrorCallback = std::function<void(const std::string &)>;

  AsyncNamedPipe(std::unique_ptr<INamedPipe> pipe);
  ~AsyncNamedPipe();

  bool start(const MessageCallback &onMessage, const ErrorCallback &onError);
  void stop();
  void asyncSend(const std::vector<uint8_t> &message);
  void wait_for_client_connection(int milliseconds);
  bool isConnected() const;

private:
  void workerThread();
  bool establishConnection();
  void processMessage(const std::vector<uint8_t> &bytes) const;
  void handleWorkerException(const std::exception &e) const;
  void handleWorkerUnknownException() const;

  std::unique_ptr<INamedPipe> _pipe;
  std::atomic<bool> _running;
  std::thread _worker;
  MessageCallback _onMessage;
  ErrorCallback _onError;
};

class WinPipe: public INamedPipe {
public:
  WinPipe(HANDLE pipe = INVALID_HANDLE_VALUE, bool isServer = false);
  ~WinPipe() override;

  bool send(std::vector<uint8_t> bytes, int timeout_ms) override;
  bool receive(std::vector<uint8_t> &bytes, int timeout_ms) override;
  void wait_for_client_connection(int milliseconds) override;
  void disconnect() override;
  bool is_connected() override;

private:
  void connect_server_pipe(int milliseconds);

  HANDLE _pipe;
  std::atomic<bool> _connected;
  bool _isServer;
};

class IAsyncPipeFactory {
public:
  virtual ~IAsyncPipeFactory() = default;
  virtual std::unique_ptr<INamedPipe> create_client(const std::string &pipeName) = 0;
  virtual std::unique_ptr<INamedPipe> create_server(const std::string &pipeName) = 0;
};

struct AnonConnectMsg {
  wchar_t pipe_name[40];
};

class NamedPipeFactory: public IAsyncPipeFactory {
public:
  std::unique_ptr<INamedPipe> create_client(const std::string &pipeName) override;
  std::unique_ptr<INamedPipe> create_server(const std::string &pipeName) override;

private:
  bool create_security_descriptor(SECURITY_DESCRIPTOR &desc, PACL *out_pacl) const;
  platf::dxgi::safe_handle create_client_pipe(const std::wstring &fullPipeName) const;
};

class AnonymousPipeFactory: public IAsyncPipeFactory {
public:
  AnonymousPipeFactory();
  std::unique_ptr<INamedPipe> create_server(const std::string &pipeName) override;
  std::unique_ptr<INamedPipe> create_client(const std::string &pipeName) override;

private:
  std::unique_ptr<NamedPipeFactory> _pipeFactory;
  std::unique_ptr<INamedPipe> handshake_server(std::unique_ptr<INamedPipe> pipe);
  std::unique_ptr<INamedPipe> handshake_client(std::unique_ptr<INamedPipe> pipe);
  std::string generateGuid() const;
};
