void AsyncNamedPipe::workerThread() {
    if (_isServer) {
        _pipe = CreateNamedPipeW(
            _pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        if (_pipe == INVALID_HANDLE_VALUE) {
            if (_onError) _onError("Failed to create named pipe");
            return;
        }
        BOOL connected = ConnectNamedPipe(_pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            if (_onError) _onError("Failed to connect named pipe");
            CloseHandle(_pipe);
            _pipe = INVALID_HANDLE_VALUE;
            return;
        }
    } else {
        while (_running) {
            _pipe = CreateFileW(
                _pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (_pipe != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_PIPE_BUSY) {
                if (_onError) _onError("Failed to open named pipe");
                return;
            }
            if (!WaitNamedPipeW(_pipeName.c_str(), 2000)) {
                if (_onError) _onError("Timed out waiting for pipe");
                return;
            }
        }
    }
    _connected = true;
    std::vector<uint8_t> buffer(4096);
    while (_running && _pipe != INVALID_HANDLE_VALUE) {
        DWORD bytesRead = 0;
        BOOL result = ReadFile(_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
        if (!result || bytesRead == 0) {
            if (_running && _onError) _onError("Pipe read error or disconnected");
            break;
        }
        if (_onMessage) {
            _onMessage(std::vector<uint8_t>(buffer.begin(), buffer.begin() + bytesRead));
        }
    }
    _connected = false;
    if (_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
}
void AsyncNamedPipe::asyncSend(const std::vector<uint8_t>& message) {
    if (!_connected || _pipe == INVALID_HANDLE_VALUE) return;
    DWORD bytesWritten = 0;
    BOOL result = WriteFile(_pipe, message.data(), static_cast<DWORD>(message.size()), &bytesWritten, nullptr);
    if (!result || bytesWritten != message.size()) {
        if (_onError) _onError("Failed to write to pipe");
    }
}

bool AsyncNamedPipe::isConnected() const {
    return _connected;
}
bool AsyncNamedPipe::start(MessageCallback onMessage, ErrorCallback onError) {
    if (_running) return false;
    _onMessage = onMessage;
    _onError = onError;
    _running = true;
    _worker = std::thread(&AsyncNamedPipe::workerThread, this);
    return true;
}

void AsyncNamedPipe::stop() {
    _running = false;
    if (_worker.joinable()) {
        _worker.join();
    }
    if (_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
    _connected = false;
}

#include "shared_memory.h"
#include <windows.h>
#include <stdexcept>
#include <iostream>

AsyncNamedPipe::AsyncNamedPipe(const std::wstring& pipeName, bool isServer)
    : _pipeName(pipeName), _pipe(INVALID_HANDLE_VALUE), _isServer(isServer), _connected(false), _running(false) {}

AsyncNamedPipe::~AsyncNamedPipe() {
    stop();
}

