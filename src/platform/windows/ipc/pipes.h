/**
 * @file src/platform/windows/ipc/pipes.h
 * @brief Windows Named and Anonymous Pipe IPC Abstractions for Sunshine
 *
 * This header defines interfaces and implementations for inter-process communication (IPC)
 * using Windows named and anonymous pipes. It provides both synchronous and asynchronous
 * APIs for sending and receiving messages, as well as factories for creating server and
 * client pipe instances with appropriate security and access control.
 */
#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// local includes
#include "src/logging.h"
#include "src/platform/windows/ipc/misc_utils.h"

// platform includes
#include <windows.h>

namespace platf::dxgi {

  constexpr uint8_t SECURE_DESKTOP_MSG = 0x01;  ///< Message type for secure desktop notifications
  constexpr uint8_t ACK_MSG = 0x02;  ///< Message type for acknowledgment responses
  constexpr uint8_t FRAME_READY_MSG = 0x03;  ///< Message type for frame ready notifications

  /**
   * @brief Structure for sharing handle and texture metadata via IPC.
   * @param texture_handle Shared texture handle.
   * @param width Width of the texture.
   * @param height Height of the texture.
   */
  struct shared_handle_data_t {
    HANDLE texture_handle;
    UINT width;
    UINT height;
  };

  /**
   * @brief Structure for configuration data shared via IPC.
   * @param dynamic_range Dynamic range setting.
   * @param log_level Logging level.
   * @param display_name Display name (wide string, max 32 chars).
   * @param adapter_luid LUID of the DXGI adapter to use for D3D11 device creation.
   */
  struct config_data_t {
    int dynamic_range;
    int log_level;
    wchar_t display_name[32];
    LUID adapter_luid;
  };

/**
 * @brief Message structure for frame ready notifications with QPC timing data.
 * This is sent by the WGC helper to the main process with a high-resolution timestamp.
 */
#pragma pack(push, 1)  // required to remove padding from compiler

  struct frame_ready_msg_t {
    uint8_t message_type = FRAME_READY_MSG;
    uint64_t frame_qpc = 0;
  };

#pragma pack(pop)  // required to remove padding from compiler

  /**
   * @brief Result codes for pipe operations.
   */
  enum class PipeResult {
    Success,  ///< Operation completed successfully
    Timeout,  ///< Operation timed out
    Disconnected,  ///< Pipe is disconnected
    BrokenPipe,  ///< Pipe is broken or invalid
    Error  ///< General error occurred
  };

  class INamedPipe {
  public:
    /**
     * @brief Virtual destructor for INamedPipe.
     */
    virtual ~INamedPipe() = default;

    /**
     * @brief Sends data through the pipe.
     * @param bytes The data to send.
     * @param timeout_ms Timeout in milliseconds for the send operation.
     * @return `true` if the data was sent successfully, `false` otherwise.
     */
    virtual bool send(std::span<const uint8_t> bytes, int timeout_ms) = 0;

    /**
     * @brief Receives data from the pipe into a span buffer.
     * @param dst Span buffer to store received data.
     * @param bytesRead Reference to store the number of bytes actually read.
     * @param timeout_ms Timeout in milliseconds for the receive operation.
     * @return `PipeResult` indicating the result of the receive operation.
     */
    virtual PipeResult receive(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) = 0;

    /**
     * @brief Flushes the message queue and retrieves the latest message from the pipe.
     * @param dst Span buffer to store the latest received data.
     * @param bytesRead Reference to store the number of bytes actually read.
     * @param timeout_ms Timeout in milliseconds for the receive operation.
     * @return `PipeResult` indicating the result of the receive operation.
     */
    virtual PipeResult receive_latest(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) = 0;

    /**
     * @brief Waits for a client to connect to the pipe.
     * @param milliseconds Timeout in milliseconds to wait for connection.
     */
    virtual void wait_for_client_connection(int milliseconds) = 0;

    /**
     * @brief Disconnects the pipe.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Checks if the pipe is connected.
     * @return `true` if connected, `false` otherwise.
     */
    virtual bool is_connected() = 0;
  };

  class AsyncNamedPipe {
  public:
    using MessageCallback = std::function<void(std::span<const uint8_t>)>;  ///< Callback for received messages
    using ErrorCallback = std::function<void(const std::string &)>;  ///< Callback for error events
    using BrokenPipeCallback = std::function<void()>;  ///< Callback for broken pipe events

    /**
     * @brief Constructs an AsyncNamedPipe with the given pipe implementation.
     * @param pipe Unique pointer to an INamedPipe instance.
     */
    AsyncNamedPipe(std::unique_ptr<INamedPipe> pipe);

    /**
     * @brief Destructor for AsyncNamedPipe. Stops the worker thread if running.
     */
    ~AsyncNamedPipe();

    /**
     * @brief Starts the asynchronous message loop.
     * @param on_message Callback for received messages.
     * @param on_error Callback for error events.
     * @param on_broken_pipe Optional callback for broken pipe events.
     * @return `true` if started successfully, `false` otherwise.
     */
    bool start(const MessageCallback &on_message, const ErrorCallback &on_error, const BrokenPipeCallback &on_broken_pipe = nullptr);

    /**
     * @brief Stops the asynchronous message loop and worker thread.
     */
    void stop();

    /**
     * @brief Sends a message asynchronously through the pipe.
     * @param message The message to send.
     */
    void send(std::span<const uint8_t> message);

    /**
     * @brief Waits for a client to connect to the pipe.
     * @param milliseconds Timeout in milliseconds to wait for connection.
     */
    void wait_for_client_connection(int milliseconds);

    /**
     * @brief Checks if the pipe is connected.
     * @return `true` if connected, `false` otherwise.
     */
    bool is_connected() const;

  private:
    /**
     * @brief Worker thread function for handling asynchronous operations.
     */
    void worker_thread() noexcept;

    /**
     * @brief Runs the main message loop for receiving and processing messages.
     */
    void run_message_loop();

    /**
     * @brief Establishes a connection with the client.
     * @return `true` if connection is established, `false` otherwise.
     */
    bool establish_connection();

    /**
     * @brief Processes a received message.
     * @param bytes The message span to process.
     */
    void process_message(std::span<const uint8_t> bytes) const;

    /**
     * @brief Safely executes an operation, catching exceptions and reporting errors.
     * @param operation_name Name of the operation for logging.
     * @param operation The operation to execute.
     */
    void safe_execute_operation(const std::string &operation_name, const std::function<void()> &operation) const noexcept;

    std::unique_ptr<INamedPipe> _pipe;
    std::atomic<bool> _running {false};
    std::jthread _worker;
    MessageCallback _onMessage;
    ErrorCallback _onError;
    BrokenPipeCallback _onBrokenPipe;
    std::array<uint8_t, 256> _buffer;  // Reusable buffer for receiving messages
  };

  class WinPipe: public INamedPipe {
  public:
    /**
     * @brief Constructs a WinPipe instance.
     * @param pipe Windows HANDLE for the pipe (default INVALID_HANDLE_VALUE).
     * @param isServer True if this is a server pipe, false if client.
     */
    WinPipe(HANDLE pipe = INVALID_HANDLE_VALUE, bool isServer = false);

    /**
     * @brief Destructor for WinPipe. Cleans up resources.
     */
    ~WinPipe() override;

    /**
     * @brief Sends data through the pipe.
     * @param bytes The data to send.
     * @param timeout_ms Timeout in milliseconds for the send operation.
     * @return `true` if the data was sent successfully, `false` otherwise.
     */
    bool send(std::span<const uint8_t> bytes, int timeout_ms) override;

    /**
     * @brief Receives data from the pipe into a span buffer.
     * @param dst Span buffer to store received data.
     * @param bytesRead Reference to store the number of bytes actually read.
     * @param timeout_ms Timeout in milliseconds for the receive operation.
     * @return `PipeResult` indicating the result of the receive operation.
     */
    PipeResult receive(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) override;

    /**
     * @brief Waits for a client to connect to the pipe.
     * @param milliseconds Timeout in milliseconds to wait for connection.
     */
    void wait_for_client_connection(int milliseconds) override;

    /**
     * @brief Disconnects the pipe.
     */
    void disconnect() override;

    /**
     * @brief Checks if the pipe is connected.
     * @return `true` if connected, `false` otherwise.
     */
    bool is_connected() override;

    /**
     * @brief Flushes the message queue and retrieves the latest message from the pipe.
     * @param dst Span buffer to store the latest received data.
     * @param bytesRead Reference to store the number of bytes actually read.
     * @param timeout_ms Timeout in milliseconds for the receive operation.
     * @return `PipeResult` indicating the result of the receive operation.
     */
    PipeResult receive_latest(std::span<uint8_t> dst, size_t &bytesRead, int timeout_ms) override;

    /**
     * @brief Flushes the pipe's buffers.
     */
    void flush_buffers();

  private:
    /**
     * @brief Connects the server pipe, waiting up to the specified time.
     * @param milliseconds Timeout in milliseconds to wait for connection.
     */
    void connect_server_pipe(int milliseconds);

    /**
     * @brief Handles a pending connection operation.
     * @param ctx IO context for the operation.
     * @param milliseconds Timeout in milliseconds.
     */
    void handle_pending_connection(std::unique_ptr<io_context> &ctx, int milliseconds);

    /**
     * @brief Handles errors during send operations.
     * @param ctx IO context for the operation.
     * @param timeout_ms Timeout in milliseconds.
     * @param bytesWritten Reference to the number of bytes written.
     * @return True if the error was handled, false otherwise.
     */
    bool handle_send_error(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten);

    /**
     * @brief Handles a pending send operation.
     * @param ctx IO context for the operation.
     * @param timeout_ms Timeout in milliseconds.
     * @param bytesWritten Reference to the number of bytes written.
     * @return True if the operation completed, false otherwise.
     */
    bool handle_pending_send_operation(std::unique_ptr<io_context> &ctx, int timeout_ms, DWORD &bytesWritten);

    /**
     * @brief Handles errors during receive operations.
     * @param ctx IO context for the operation.
     * @param timeout_ms Timeout in milliseconds.
     * @param dst Destination span for receiving data.
     * @param bytesRead Reference to store the number of bytes read.
     * @return PipeResult indicating the result of the receive operation.
     */
    PipeResult handle_receive_error(std::unique_ptr<io_context> &ctx, int timeout_ms, std::span<uint8_t> dst, size_t &bytesRead);

    /**
     * @brief Handles a pending receive operation.
     * @param ctx IO context for the operation.
     * @param timeout_ms Timeout in milliseconds.
     * @param dst Destination span for receiving data.
     * @param bytesRead Reference to store the number of bytes read.
     * @return PipeResult indicating the result of the receive operation.
     */
    PipeResult handle_pending_receive_operation(std::unique_ptr<io_context> &ctx, int timeout_ms, std::span<uint8_t> dst, size_t &bytesRead);

    HANDLE _pipe;
    std::atomic<bool> _connected {false};
    bool _is_server;
  };

  class IAsyncPipeFactory {
  public:
    /**
     * @brief Virtual destructor for IAsyncPipeFactory.
     */
    virtual ~IAsyncPipeFactory() = default;

    /**
     * @brief Creates a client pipe instance.
     * @param pipe_name The name of the pipe to connect to.
     * @return Unique pointer to the created INamedPipe instance.
     */
    virtual std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) = 0;

    /**
     * @brief Creates a server pipe instance.
     * @param pipe_name The name of the pipe to create.
     * @return Unique pointer to the created INamedPipe instance.
     */
    virtual std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) = 0;
  };

  /**
   * @brief Message structure for anonymous pipe connection handshake.
   * @param pipe_name Wide character pipe name for connection (max 40 chars).
   */
  struct AnonConnectMsg {
    wchar_t pipe_name[40];
  };

  class NamedPipeFactory: public IAsyncPipeFactory {
  public:
    /**
     * @brief Creates a client named pipe.
     * @param pipe_name The name of the pipe to connect to.
     * @return Unique pointer to the created INamedPipe instance.
     */
    std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) override;

    /**
     * @brief Creates a server named pipe.
     * @param pipe_name The name of the pipe to create.
     * @return Unique pointer to the created INamedPipe instance.
     */
    std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) override;

  private:
    /**
     * @brief Creates a security descriptor for the pipe.
     * @param desc Reference to a SECURITY_DESCRIPTOR to initialize.
     * @param out_pacl Output pointer to the created PACL.
     * @return `true` if successful, `false` otherwise.
     */
    bool create_security_descriptor(SECURITY_DESCRIPTOR &desc, PACL *out_pacl) const;

    /**
     * @brief Obtains an access token for the current or system user.
     * @param isSystem True to obtain the system token, false for current user.
     * @param token Output safe_token to receive the token.
     * @return `true` if successful, `false` otherwise.
     */
    bool obtain_access_token(BOOL isSystem, safe_token &token) const;

    /**
     * @brief Extracts the user SID from a token.
     * @param token The token to extract from.
     * @param tokenUser Output pointer to TOKEN_USER structure.
     * @param raw_user_sid Output pointer to the raw user SID.
     * @return `true` if successful, `false` otherwise.
     */
    bool extract_user_sid_from_token(const safe_token &token, util::c_ptr<TOKEN_USER> &tokenUser, PSID &raw_user_sid) const;

    /**
     * @brief Creates a SID for the system account.
     * @param system_sid Output safe_sid to receive the system SID.
     * @return `true` if successful, `false` otherwise.
     */
    bool create_system_sid(safe_sid &system_sid) const;

    /**
     * @brief Builds an access control list for the pipe.
     * @param isSystem True if for system, false for user.
     * @param desc Reference to a SECURITY_DESCRIPTOR.
     * @param raw_user_sid Pointer to the user SID.
     * @param system_sid Pointer to the system SID.
     * @param out_pacl Output pointer to the created PACL.
     * @return `true` if successful, `false` otherwise.
     */
    bool build_access_control_list(BOOL isSystem, SECURITY_DESCRIPTOR &desc, PSID raw_user_sid, PSID system_sid, PACL *out_pacl) const;

    /**
     * @brief Creates a client pipe handle.
     * @param fullPipeName The full name of the pipe.
     * @return `safe_handle` to the created client pipe.
     */
    safe_handle create_client_pipe(const std::wstring &fullPipeName) const;
  };

  class AnonymousPipeFactory: public IAsyncPipeFactory {
  public:
    /**
     * @brief Constructs an AnonymousPipeFactory instance.
     */
    AnonymousPipeFactory();

    /**
     * @brief Creates a server anonymous pipe.
     * @param pipe_name The name of the pipe to create.
     * @return Unique pointer to the created INamedPipe instance.
     */
    std::unique_ptr<INamedPipe> create_server(const std::string &pipe_name) override;

    /**
     * @brief Creates a client anonymous pipe.
     * @param pipe_name The name of the pipe to connect to.
     * @return Unique pointer to the created INamedPipe instance.
     */
    std::unique_ptr<INamedPipe> create_client(const std::string &pipe_name) override;

  private:
    std::unique_ptr<NamedPipeFactory> _pipe_factory = std::make_unique<NamedPipeFactory>();

    /**
     * @brief Performs the handshake as a server, establishing the connection.
     * @param pipe Unique pointer to the server INamedPipe.
     * @return `Unique pointer` to the established INamedPipe.
     */
    std::unique_ptr<INamedPipe> handshake_server(std::unique_ptr<INamedPipe> pipe);

    /**
     * @brief Performs the handshake as a client, establishing the connection.
     * @param pipe Unique pointer to the client INamedPipe.
     * @return `Unique pointer` to the established INamedPipe.
     */
    std::unique_ptr<INamedPipe> handshake_client(std::unique_ptr<INamedPipe> pipe);

    /**
     * @brief Sends a handshake message through the pipe.
     * @param pipe Reference to the pipe to send the message through.
     * @param pipe_name The name of the pipe for the handshake.
     * @return `true` if the message was sent successfully, `false` otherwise.
     */
    bool send_handshake_message(std::unique_ptr<INamedPipe> &pipe, const std::string &pipe_name) const;

    /**
     * @brief Waits for a handshake acknowledgment from the other side.
     * @param pipe Reference to the pipe to wait on.
     * @return `true` if acknowledgment was received, `false` otherwise.
     */
    bool wait_for_handshake_ack(std::unique_ptr<INamedPipe> &pipe) const;

    /**
     * @brief Receives a handshake message from the pipe.
     * @param pipe Reference to the pipe to receive from.
     * @param msg Output AnonConnectMsg to store the received message.
     * @return `true` if the message was received successfully, `false` otherwise.
     */
    bool receive_handshake_message(std::unique_ptr<INamedPipe> &pipe, AnonConnectMsg &msg) const;

    /**
     * @brief Sends a handshake acknowledgment through the pipe.
     * @param pipe Reference to the pipe to send the acknowledgment through.
     * @return `true` if the acknowledgment was sent successfully, `false` otherwise.
     */
    bool send_handshake_ack(std::unique_ptr<INamedPipe> &pipe) const;

    /**
     * @brief Connects to the data pipe after handshake.
     * @param pipeNameStr The name of the data pipe to connect to.
     * @return `Unique pointer` to the connected INamedPipe.
     */
    std::unique_ptr<INamedPipe> connect_to_data_pipe(const std::string &pipeNameStr);
  };
}  // namespace platf::dxgi
