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
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        if (_pipe == INVALID_HANDLE_VALUE) {
            std::wcout << L"[AsyncNamedPipe] Failed to create named pipe: " << _pipeName << L" error=" << GetLastError() << std::endl;
            if (_onError) {
                try {
                    _onError("Failed to create named pipe");
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                }
            }
            return;
        }
        std::wcout << L"[AsyncNamedPipe] Server waiting for client to connect: " << _pipeName << std::endl;
        
        // Use overlapped I/O to allow checking _running flag during ConnectNamedPipe
        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            std::wcout << L"[AsyncNamedPipe] Failed to create event for overlapped I/O" << std::endl;
            if (_onError) {
                try {
                    _onError("Failed to create event for overlapped I/O");
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                }
            }
            CloseHandle(_pipe);
            _pipe = INVALID_HANDLE_VALUE;
            return;
        }
        
        BOOL connected = ConnectNamedPipe(_pipe, &overlapped);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Connection is pending, wait with timeout while checking _running
                while (_running) {
                    DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 100); // 100ms timeout
                    if (waitResult == WAIT_OBJECT_0) {
                        // Connection completed
                        DWORD bytesTransferred;
                        if (GetOverlappedResult(_pipe, &overlapped, &bytesTransferred, FALSE)) {
                            connected = TRUE;
                        } else {
                            err = GetLastError();
                        }
                        break;
                    } else if (waitResult == WAIT_TIMEOUT) {
                        // Continue checking _running
                        continue;
                    } else {
                        // Error occurred
                        err = GetLastError();
                        break;
                    }
                }
                
                if (!_running) {
                    // Stop was called, cancel the operation
                    CancelIo(_pipe);
                    CloseHandle(overlapped.hEvent);
                    if (_onError) {
                        try {
                            _onError("Pipe operation cancelled");
                        } catch (const std::exception& e) {
                            std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                        }
                    }
                    CloseHandle(_pipe);
                    _pipe = INVALID_HANDLE_VALUE;
                    return;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                std::wcout << L"[AsyncNamedPipe] Failed to connect named pipe: " << _pipeName << L" error=" << err << std::endl;
                if (_onError) {
                    try {
                        _onError("Failed to connect named pipe");
                    } catch (const std::exception& e) {
                        std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                    }
                }
                CloseHandle(overlapped.hEvent);
                CloseHandle(_pipe);
                _pipe = INVALID_HANDLE_VALUE;
                return;
            } else {
                connected = TRUE;
            }
        }
        
        CloseHandle(overlapped.hEvent);
        
        if (!connected) {
            std::wcout << L"[AsyncNamedPipe] Failed to connect named pipe: " << _pipeName << L" error=" << GetLastError() << std::endl;
            if (_onError) {
                try {
                    _onError("Failed to connect named pipe");
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                }
            }
            CloseHandle(_pipe);
            _pipe = INVALID_HANDLE_VALUE;
            return;
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
            if (_onError) {
                try {
                    _onError("Failed to connect to server pipe");
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                }
            }
            return;
        }
    }

    std::vector<uint8_t> buffer(4096);
    while (_running && _connected) {
        DWORD read = 0;
        DWORD totalAvailable = 0;
        
        // Check if there's data available before attempting to read
        if (!PeekNamedPipe(_pipe, nullptr, 0, nullptr, &totalAvailable, nullptr)) {
            std::wcout << L"[AsyncNamedPipe] PeekNamedPipe failed: " << _pipeName << L" error=" << GetLastError() << std::endl;
            if (_running && _onError) {
                try {
                    _onError("Pipe peek error or closed");
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                }
            }
            _connected = false;
            break;
        }
        
        if (totalAvailable > 0) {
            BOOL ok = ReadFile(_pipe, buffer.data(), (DWORD)buffer.size(), &read, nullptr);
            if (!ok || read == 0) {
                std::wcout << L"[AsyncNamedPipe] ReadFile failed or closed: " << _pipeName << L" ok=" << ok << L" read=" << read << std::endl;
                if (_running && _onError) {
                    try {
                        _onError("Pipe read error or closed");
                    } catch (const std::exception& e) {
                        std::wcout << L"[AsyncNamedPipe] Exception in error callback: " << e.what() << std::endl;
                    }
                }
                _connected = false;
                break;
            }
            if (_onMessage) {
                try {
                    _onMessage(std::vector<uint8_t>(buffer.begin(), buffer.begin() + read));
                } catch (const std::exception& e) {
                    std::wcout << L"[AsyncNamedPipe] Exception in message callback: " << e.what() << std::endl;
                    // Continue processing despite callback exception
                }
            }
        } else {
            // No data available, sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (_pipe != INVALID_HANDLE_VALUE) {
        std::wcout << L"[AsyncNamedPipe] Closing pipe (worker): " << _pipeName << std::endl;
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
    _connected = false;
}
