#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>


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
   * @brief Starts an asynchronous read operation.
   */
  virtual void begin_async_read() = 0;
};

class AsyncNamedPipe {
public:
  using MessageCallback = std::function<void(const std::vector<uint8_t> &)>;
  using ErrorCallback = std::function<void(const std::string &)>;

  AsyncNamedPipe(std::unique_ptr<IAsyncPipe> pipe);
  ~AsyncNamedPipe();

  bool start(MessageCallback onMessage, ErrorCallback onError);
  void stop();
  void asyncSend(const std::vector<uint8_t> &message);
  void wait_for_client_connection(int milliseconds);
  bool isConnected() const;

private:
  void workerThread();

  std::unique_ptr<IAsyncPipe> _pipe;
  std::atomic<bool> _running;
  std::thread _worker;
  MessageCallback _onMessage;
  ErrorCallback _onError;
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

  // New methods for async read operations
  void begin_async_read();
  void complete_async_read(std::vector<uint8_t> &bytes);

private:
  HANDLE _pipe;
  HANDLE _event;
  std::atomic<bool> _connected;
  bool _isServer;
  std::atomic<bool> _running;
  
  // New members for persistent async operations
  OVERLAPPED _readOverlapped;
  std::vector<uint8_t> _readBuffer;
  std::atomic<bool> _readPending;
};

class IAsyncPipeFactory {
public:
  virtual ~IAsyncPipeFactory() = default;
  virtual std::unique_ptr<IAsyncPipe> create(const std::string &pipeName, const std::string &eventName, bool isServer, bool isSecured) = 0;
};

struct SecureClientMessage {
  wchar_t pipe_name[40];
  wchar_t event_name[40];
};

class SecuredPipeCoordinator {
public:
  SecuredPipeCoordinator(IAsyncPipeFactory *pipeFactory);
  std::unique_ptr<IAsyncPipe> prepare_client(std::unique_ptr<IAsyncPipe> pipe);
  std::unique_ptr<IAsyncPipe> prepare_server(std::unique_ptr<IAsyncPipe> pipe);

private:
  std::string generateGuid();
  IAsyncPipeFactory *_pipeFactory;
};

class SecuredPipeFactory: public IAsyncPipeFactory {
public:
  SecuredPipeFactory();
  std::unique_ptr<IAsyncPipe> create(const std::string &pipeName, const std::string &eventName, bool isServer, bool isSecured) override;

private:
  std::unique_ptr<IAsyncPipeFactory> _pipeFactory;
  SecuredPipeCoordinator _coordinator;
};

class AsyncPipeFactory: public IAsyncPipeFactory {
public:
  std::unique_ptr<IAsyncPipe> create(const std::string &pipeName, const std::string &eventName, bool isServer, bool isSecured) override;

private:
  bool create_security_descriptor(SECURITY_DESCRIPTOR &desc);
  bool create_security_descriptor_for_target_process(SECURITY_DESCRIPTOR &desc, DWORD target_pid);
};
