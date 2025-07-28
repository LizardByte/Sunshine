#pragma once

#include "src/logging.h"
#include "src/platform/windows/wgc/misc_utils.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace platf::dxgi {

  constexpr uint8_t HEARTBEAT_MSG = 0x01;
  constexpr uint8_t FRAME_READY_MSG = 0x03;
  constexpr uint8_t SECURE_DESKTOP_MSG = 0x02;
  constexpr uint8_t ACK_MSG = 0xA5;

  /**
   * @brief Result codes for pipe operations
   */
  enum class PipeResult {
    Success,  // Operation completed successfully
    Timeout,  // Operation timed out
    Disconnected,  // Pipe was disconnected
    BrokenPipe,  // Pipe broken (ERROR_BROKEN_PIPE = 109) - requires reinit
    Error  // General error
  };

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
     * @return PipeResult indicating the outcome of the operation.
     */
    virtual PipeResult receive(std::vector<uint8_t> &bytes, int timeout_ms) = 0;

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
    using BrokenPipeCallback = std::function<void()>;  // New callback for broken pipe

    AsyncNamedPipe(std::unique_ptr<INamedPipe> pipe);
    ~AsyncNamedPipe();

    bool start(const MessageCallback &onMessage, const ErrorCallback &onError, const BrokenPipeCallback &onBrokenPipe = nullptr);
    void stop();
    void send(const std::vector<uint8_t> &message);
    void wait_for_client_connection(int milliseconds);
    bool is_connected() const;

  private:
    void worker_thread() noexcept;
    void run_message_loop();
    bool establish_connection();
    void process_message(const std::vector<uint8_t> &bytes) const;
    void safe_execute_operation(const std::string &operation_name, const std::function<void()> &operation) const noexcept;

    std::unique_ptr<INamedPipe> _pipe;
    std::atomic<bool> _running;
    std::thread _worker;
    MessageCallback _onMessage;
    ErrorCallback _onError;
    BrokenPipeCallback _onBrokenPipe;  // New callback member
  };

  class WinPipe: public INamedPipe {
  public:
    WinPipe(HANDLE pipe = INVALID_HANDLE_VALUE, bool isServer = false);
    ~WinPipe() override;

    bool send(std::vector<uint8_t> bytes, int timeout_ms) override;
    PipeResult receive(std::vector<uint8_t> &bytes, int timeout_ms) override;
    void wait_for_client_connection(int milliseconds) override;
    void disconnect() override;
    bool is_connected() override;
    void flush_buffers();

  private:
    void connect_server_pipe(int milliseconds);
    bool handle_send_error(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten);
    bool handle_pending_send_operation(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten);
    PipeResult handle_receive_error(std::unique_ptr<io_context> &ctx, int timeout_ms, 
                                   std::vector<uint8_t> &buffer, std::vector<uint8_t> &bytes);
    PipeResult handle_pending_receive_operation(std::unique_ptr<io_context> &ctx, int timeout_ms,
                                               std::vector<uint8_t> &buffer, std::vector<uint8_t> &bytes);

    HANDLE _pipe;
    std::atomic<bool> _connected;
    bool _is_server;
  };

  class IAsyncPipeFactory {
  public:
    virtual ~IAsyncPipeFactory() = default;
    virtual std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) = 0;
    virtual std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) = 0;
  };

  struct AnonConnectMsg {
    wchar_t pipe_name[40];
  };

  class NamedPipeFactory: public IAsyncPipeFactory {
  public:
    std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) override;
    std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) override;

  private:
    bool create_security_descriptor(SECURITY_DESCRIPTOR &desc, PACL *out_pacl) const;
    bool obtain_access_token(BOOL isSystem, safe_token &token) const;
    bool extract_user_sid_from_token(const safe_token &token, util::c_ptr<TOKEN_USER> &tokenUser, PSID &raw_user_sid) const;
    bool create_system_sid(safe_sid &system_sid) const;
    void log_sid_for_debugging(PSID sid, const std::string &sidType) const;
    bool build_access_control_list(BOOL isSystem, SECURITY_DESCRIPTOR &desc, PSID raw_user_sid, PSID system_sid, PACL *out_pacl) const;
    safe_handle create_client_pipe(const std::wstring &fullPipeName) const;
  };

  class AnonymousPipeFactory: public IAsyncPipeFactory {
  public:
    AnonymousPipeFactory();
    std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) override;
    std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) override;

  private:
    std::unique_ptr<NamedPipeFactory> _pipe_factory;
    std::unique_ptr<INamedPipe> handshake_server(std::unique_ptr<INamedPipe> pipe);
    std::unique_ptr<INamedPipe> handshake_client(std::unique_ptr<INamedPipe> pipe);
    bool send_handshake_message(std::unique_ptr<INamedPipe> &pipe, const std::string &pipe_name);
    bool wait_for_handshake_ack(std::unique_ptr<INamedPipe> &pipe);
    bool receive_handshake_message(std::unique_ptr<INamedPipe> &pipe, AnonConnectMsg &msg);
    bool send_handshake_ack(std::unique_ptr<INamedPipe> &pipe);
    std::unique_ptr<INamedPipe> connect_to_data_pipe(const std::string &pipeNameStr);
    std::string generate_guid() const;
  };
}  // namespace platf::dxgi
