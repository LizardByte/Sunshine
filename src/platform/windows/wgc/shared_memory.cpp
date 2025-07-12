#include "shared_memory.h"
#include <iostream>


AsyncNamedPipe::AsyncNamedPipe(const std::wstring& pipeName, bool isServer)
    : _pipeName(pipeName), _pipe(INVALID_HANDLE_VALUE), _isServer(isServer), _connected(false), _running(false) {
    std::wcout << L"[AsyncNamedPipe] Constructed: " << _pipeName << L" isServer=" << isServer << std::endl;
}

AsyncNamedPipe::~AsyncNamedPipe() {
    stop();
}

bool AsyncNamedPipe::start(MessageCallback onMessage, ErrorCallback onError) {
    if (_running) return false;
    _onMessage = onMessage;
    _onError = onError;
    _running = true;
    std::wcout << L"[AsyncNamedPipe] Starting worker thread for: " << _pipeName << std::endl;
    _worker = std::thread(&AsyncNamedPipe::workerThread, this);
    return true;
}

void AsyncNamedPipe::stop() {
    _running = false;
    if (_worker.joinable()) _worker.join();
    if (_pipe != INVALID_HANDLE_VALUE) {
        std::wcout << L"[AsyncNamedPipe] Closing pipe: " << _pipeName << std::endl;
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
    _connected = false;
}

void AsyncNamedPipe::asyncSend(const std::vector<uint8_t>& message) {
    if (!_connected || _pipe == INVALID_HANDLE_VALUE) {
        std::wcout << L"[AsyncNamedPipe] asyncSend failed: not connected for " << _pipeName << std::endl;
        return;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(_pipe, message.data(), (DWORD)message.size(), &written, nullptr);
    std::wcout << L"[AsyncNamedPipe] asyncSend: wrote " << written << L" bytes, ok=" << ok << std::endl;
}

bool AsyncNamedPipe::isConnected() const {
    return _connected;
}

void AsyncNamedPipe::workerThread() {
    if (_isServer) {
        std::wcout << L"[AsyncNamedPipe] Server creating named pipe: " << _pipeName << std::endl;
        _pipe = CreateNamedPipeW(
            _pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        if (_pipe == INVALID_HANDLE_VALUE) {
            std::wcout << L"[AsyncNamedPipe] Failed to create named pipe: " << _pipeName << L" error=" << GetLastError() << std::endl;
            if (_onError) _onError("Failed to create named pipe");
            return;
        }
        std::wcout << L"[AsyncNamedPipe] Server waiting for client to connect: " << _pipeName << std::endl;
        if (!ConnectNamedPipe(_pipe, nullptr)) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                std::wcout << L"[AsyncNamedPipe] Failed to connect named pipe: " << _pipeName << L" error=" << err << std::endl;
                if (_onError) _onError("Failed to connect named pipe");
                CloseHandle(_pipe);
                _pipe = INVALID_HANDLE_VALUE;
                return;
            }
        }
        std::wcout << L"[AsyncNamedPipe] Server connected: " << _pipeName << std::endl;
        _connected = true;
    } else {
        std::wcout << L"[AsyncNamedPipe] Client attempting to connect: " << _pipeName << std::endl;
        while (_running && !_connected) {
            _pipe = CreateFileW(_pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (_pipe != INVALID_HANDLE_VALUE) {
                std::wcout << L"[AsyncNamedPipe] Client connected: " << _pipeName << std::endl;
                _connected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!_connected) {
            std::wcout << L"[AsyncNamedPipe] Client failed to connect: " << _pipeName << std::endl;
            if (_onError) _onError("Failed to connect to server pipe");
            return;
        }
    }

    std::vector<uint8_t> buffer(4096);
    while (_running && _connected) {
        DWORD read = 0;
        BOOL ok = ReadFile(_pipe, buffer.data(), (DWORD)buffer.size(), &read, nullptr);
        if (!ok || read == 0) {
            std::wcout << L"[AsyncNamedPipe] ReadFile failed or closed: " << _pipeName << L" ok=" << ok << L" read=" << read << std::endl;
            if (_running && _onError) _onError("Pipe read error or closed");
            _connected = false;
            break;
        }
        std::wcout << L"[AsyncNamedPipe] Received " << read << L" bytes on " << _pipeName << std::endl;
        if (_onMessage) _onMessage(std::vector<uint8_t>(buffer.begin(), buffer.begin() + read));
    }
    if (_pipe != INVALID_HANDLE_VALUE) {
        std::wcout << L"[AsyncNamedPipe] Closing pipe (worker): " << _pipeName << std::endl;
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
    _connected = false;
}
