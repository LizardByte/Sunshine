#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <windows.h>

class AsyncNamedPipe {
public:
    using MessageCallback = std::function<void(const std::vector<uint8_t>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    AsyncNamedPipe(const std::wstring& pipeName, bool isServer);
    ~AsyncNamedPipe();

    bool start(MessageCallback onMessage, ErrorCallback onError);
    void stop();

    void asyncSend(const std::vector<uint8_t>& message);

    bool isConnected() const;

private:
    void workerThread();

    std::wstring _pipeName;
    HANDLE _pipe;
    bool _isServer;
    std::atomic<bool> _connected;
    std::atomic<bool> _running;
    std::thread _worker;
    MessageCallback _onMessage;
    ErrorCallback _onError;
};
