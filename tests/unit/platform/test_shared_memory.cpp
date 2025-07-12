#include "../../../src/platform/windows/wgc/shared_memory.h"
#include <gtest/gtest.h>

#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <future>

// Mock callback helpers
struct CallbackFlags {
    std::atomic<bool> called{false};
    std::vector<uint8_t> lastMsg;
    std::string lastError;
};

void onMessageStore(const std::vector<uint8_t>& msg, CallbackFlags* flags) {
    flags->called = true;
    flags->lastMsg = msg;
}
void onErrorStore(const std::string& err, CallbackFlags* flags) {
    flags->called = true;
    flags->lastError = err;
}


// Helper to wait for connection with timeout, returns true if connected
bool waitForConnection(AsyncNamedPipe& pipe, int timeoutMs = 2000) {
    int waited = 0;
    while (!pipe.isConnected() && waited < timeoutMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
    }
    return pipe.isConnected();
}

// Helper: deadlock protection lambda
// Usage: deadlock_protection([&]{ /* test body */ });
auto deadlock_protection = [](auto&& fn) {
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    std::atomic<bool> timed_out{false};
    std::thread t([done, &fn, &timed_out]{
        try {
            fn();
            if (!timed_out.load()) {
                done->set_value();
            }
        } catch (...) {
            if (!timed_out.load()) {
                done->set_exception(std::current_exception());
            }
        }
    });
    if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
        timed_out = true;
        t.detach();
        FAIL() << "Test deadlocked or took too long (>3s)";
    } else {
        t.join();
        // propagate exception if any
        fut.get();
    }
};


TEST(AsyncNamedPipe, ServerClientConnectsAndSendsMessage) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeA";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe server(pipeName, true);
        AsyncNamedPipe client(pipeName, false);
        std::vector<uint8_t> received;
        bool error = false;

        server.start([&](const std::vector<uint8_t>& msg) { received = msg; }, [&](const std::string&) { error = true; });
        client.start([](const std::vector<uint8_t>&) {}, [&](const std::string&) { error = true; });

        ASSERT_TRUE(waitForConnection(server, 2000)) << "Server did not connect in time";
        ASSERT_TRUE(waitForConnection(client, 2000)) << "Client did not connect in time";

        std::vector<uint8_t> msg = {1,2,3,4,5};
        client.asyncSend(msg);

        int waited = 0;
        while (received.empty() && waited < 1000 && !error) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }

        server.stop();
        client.stop();

        ASSERT_FALSE(error) << "Error callback triggered during test";
        ASSERT_EQ(received, msg) << "Message not received in time or incorrect";
    });
}

TEST(AsyncNamedPipe, DoubleStartStop) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeB";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe pipe(pipeName, true);
        CallbackFlags flags;
        ASSERT_TRUE(pipe.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string&){}));
        ASSERT_FALSE(pipe.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string&){}));
        pipe.stop();
        pipe.stop(); // Should be safe
    });
}

TEST(AsyncNamedPipe, ServerPipeCreationFailure) {
    deadlock_protection([&] {
        // Use an invalid pipe name to force failure
        std::wstring badName = L"INVALID_PIPE_NAME";
        AsyncNamedPipe pipe(badName, true);
        CallbackFlags flags;
        pipe.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string& msg){ onErrorStore(msg, &flags); }
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pipe.stop();
        ASSERT_TRUE(flags.called);
        ASSERT_FALSE(flags.lastError.empty());
    });
}

TEST(AsyncNamedPipe, ClientConnectRetryFailure) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeC";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe pipe(pipeName, false);
        CallbackFlags flags;
        pipe.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string& msg){ onErrorStore(msg, &flags); }
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pipe.stop();
        ASSERT_TRUE(flags.called);
        ASSERT_FALSE(flags.lastError.empty());
    });
}

TEST(AsyncNamedPipe, SendReceiveRoundtrip) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeD";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe server(pipeName, true);
        AsyncNamedPipe client(pipeName, false);
        CallbackFlags serverFlags;
        server.start(
            [&](const std::vector<uint8_t>& msg){ onMessageStore(msg, &serverFlags); },
            [&](const std::string& msg){ onErrorStore(msg, &serverFlags); }
        );
        client.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string&){ }
        );
        ASSERT_TRUE(waitForConnection(server, 2000));
        ASSERT_TRUE(waitForConnection(client, 2000));
        std::vector<uint8_t> msg = {9,8,7,6};
        client.asyncSend(msg);
        int waited = 0;
        while (!serverFlags.called && waited < 1000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
        client.stop();
        server.stop();
        ASSERT_TRUE(serverFlags.called);
        ASSERT_EQ(serverFlags.lastMsg, msg);
    });
}

TEST(AsyncNamedPipe, SendFailsIfNotConnected) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeE";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe pipe(pipeName, true);
        // Not started, not connected
        pipe.asyncSend({1,2,3});
        // Should not crash or throw
    });
}

TEST(AsyncNamedPipe, ErrorCallbackOnPipeError) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeF";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe server(pipeName, true);
        CallbackFlags flags;
        server.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string& msg){ onErrorStore(msg, &flags); }
        );
        server.stop(); // Should trigger error callback
        ASSERT_TRUE(flags.called || !flags.lastError.empty());
    });
}

TEST(AsyncNamedPipe, BufferSizeLimit) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeG";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe server(pipeName, true);
        CallbackFlags flags;
        server.start(
            [&](const std::vector<uint8_t>& msg){
                flags.called = true;
                ASSERT_LE(msg.size(), 4096);
            },
            [&](const std::string&){ }
        );
        AsyncNamedPipe client(pipeName, false);
        client.start(
            [&](const std::vector<uint8_t>&){},
            [&](const std::string&){ }
        );
        ASSERT_TRUE(waitForConnection(server, 2000));
        ASSERT_TRUE(waitForConnection(client, 2000));
        std::vector<uint8_t> bigMsg(5000, 0x42);
        client.asyncSend(bigMsg);
        int waited = 0;
        while (!flags.called && waited < 1000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
        client.stop();
        server.stop();
        ASSERT_TRUE(flags.called);
    });
}

TEST(AsyncNamedPipe, CallbackExceptionSafety) {
    deadlock_protection([&] {
        std::wstring pipeName = L"\\\\.\\pipe\\testpipeH";
        std::wcout << L"[TEST] Using pipe name: " << pipeName << std::endl;
        AsyncNamedPipe pipe(pipeName, true);
        pipe.start(
            [&](const std::vector<uint8_t>&){ 
                std::wcout << L"[TEST] Message callback called, throwing exception" << std::endl;
                throw std::runtime_error("fail"); 
            },
            [&](const std::string&){ 
                std::wcout << L"[TEST] Error callback called, throwing exception" << std::endl;
                throw std::runtime_error("fail"); 
            }
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pipe.stop();
    });
}
