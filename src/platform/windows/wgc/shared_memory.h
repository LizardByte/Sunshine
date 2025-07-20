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

// Forward declaration for RAII wrapper
namespace platf::dxgi {
  struct safe_handle;
}

class IAsyncPipe {
public:
  /**
   * @brief Virtual destructor for safe polymorphic deletion.
   */
  virtual ~IAsyncPipe() = default;

  /**
   * @brief Sends a message through the pipe.
   * @param bytes The message to send as a vector of bytes.
   *
   * May be asynchronous or blocking depending on implementation.
   */
  virtual void send(std::vector<uint8_t> bytes) = 0;

  /**
   * @brief Sends a message through the pipe.
   * @param bytes The message to send as a vector of bytes.
   * @param block If true, the call blocks until the message is sent; if false, it may return immediately.
   */
  virtual void send(const std::vector<uint8_t> &bytes, bool block) = 0;

  /**
   * @brief Receives a message from the pipe.
   * @param bytes The received message will be stored in this vector.
   *
   * May be asynchronous or blocking depending on implementation.
   */
  virtual void receive(std::vector<uint8_t> &bytes) = 0;

  /**
   * @brief Receives a message from the pipe.
   * @param bytes The received message will be stored in this vector.
   * @param block If true, the call blocks until a message is received; if false, it may return immediately.
   */
  virtual void receive(std::vector<uint8_t> &bytes, bool block) = 0;

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

  /**
   * @brief Performs an overlapped read operation.
   * @param buffer Buffer to read data into.
   * @param overlapped OVERLAPPED structure for the operation.
   * @return True if the operation was initiated successfully, false otherwise.
   */
  virtual bool read_overlapped(std::vector<uint8_t> &buffer, OVERLAPPED *overlapped) = 0;

  /**
   * @brief Gets the result of an overlapped operation.
   * @param overlapped OVERLAPPED structure from the operation.
   * @param bytesRead Output parameter for number of bytes read.
   * @return True if the operation completed successfully, false otherwise.
   */
  virtual bool get_overlapped_result(OVERLAPPED *overlapped, DWORD &bytesRead) = 0;
};

class AsyncNamedPipe {
public:
  using MessageCallback = std::function<void(const std::vector<uint8_t> &)>;
  using ErrorCallback = std::function<void(const std::string &)>;

  AsyncNamedPipe(std::unique_ptr<IAsyncPipe> pipe);
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
  bool postRead();

  std::unique_ptr<IAsyncPipe> _pipe;
  std::atomic<bool> _running;
  std::thread _worker;
  MessageCallback _onMessage;
  ErrorCallback _onError;
  std::vector<uint8_t> _rxBuf;
  OVERLAPPED _ovl;
};

class AsyncPipe: public IAsyncPipe {
public:
  AsyncPipe(HANDLE pipe = INVALID_HANDLE_VALUE, HANDLE event = nullptr, bool isServer = false);
  ~AsyncPipe() override;

  void send(std::vector<uint8_t> bytes) override;
  void send(const std::vector<uint8_t> &bytes, bool block) override;
  void receive(std::vector<uint8_t> &bytes) override;
  void receive(std::vector<uint8_t> &bytes, bool block) override;
  void wait_for_client_connection(int milliseconds) override;
  void disconnect() override;
  bool is_connected() override;
  bool read_overlapped(std::vector<uint8_t> &buffer, OVERLAPPED *overlapped) override;
  bool get_overlapped_result(OVERLAPPED *overlapped, DWORD &bytesRead) override;

private:
  void connect_server_pipe(OVERLAPPED &ovl, int milliseconds);

  HANDLE _pipe;
  HANDLE _event;
  std::atomic<bool> _connected;
  bool _isServer;
};

class IAsyncPipeFactory {
public:
  virtual ~IAsyncPipeFactory() = default;
  virtual std::unique_ptr<IAsyncPipe> create_client(const std::string &pipeName, const std::string &eventName) = 0;
  virtual std::unique_ptr<IAsyncPipe> create_server(const std::string &pipeName, const std::string &eventName) = 0;
};

struct AnonConnectMsg {
  wchar_t pipe_name[40];
  wchar_t event_name[40];
};


class AsyncPipeFactory: public IAsyncPipeFactory {
public:
  std::unique_ptr<IAsyncPipe> create_client(const std::string &pipeName, const std::string &eventName) override;
  std::unique_ptr<IAsyncPipe> create_server(const std::string &pipeName, const std::string &eventName) override;

private:
  bool create_security_descriptor(SECURITY_DESCRIPTOR &desc) const;
  bool create_security_descriptor_for_target_process(SECURITY_DESCRIPTOR &desc, DWORD target_pid) const;
  platf::dxgi::safe_handle create_client_pipe(const std::wstring &fullPipeName) const;
};

class AnonymousPipeConnector: public IAsyncPipeFactory {
public:
  AnonymousPipeConnector();
  std::unique_ptr<IAsyncPipe> create_server(const std::string &pipeName, const std::string &eventName) override;
  std::unique_ptr<IAsyncPipe> create_client(const std::string &pipeName, const std::string &eventName) override;

private:
  std::unique_ptr<AsyncPipeFactory> _pipeFactory;
  std::unique_ptr<IAsyncPipe> handshake_server(std::unique_ptr<IAsyncPipe> pipe);
  std::unique_ptr<IAsyncPipe> handshake_client(std::unique_ptr<IAsyncPipe> pipe);
  std::string generateGuid() const;
};
