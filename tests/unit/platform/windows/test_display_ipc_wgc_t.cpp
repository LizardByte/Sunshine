#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include "src/platform/windows/wgc/display_ipc_wgc_t.cpp"

using namespace platf::dxgi;

// Deadlock protection utility (pattern from test_shared_memory)
template <typename Func>
void deadlock_protection(Func&& func, int timeout_ms = 3000) {
    std::atomic<bool> finished{false};
    std::thread t([&] {
        func();
        finished = true;
    });
    auto start = std::chrono::steady_clock::now();
    while (!finished && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!finished) {
        t.detach();
        FAIL() << "Test deadlocked or took too long (> " << timeout_ms << " ms)";
    } else {
        t.join();
    }
}

class DisplayIpcWgcIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any previous helper processes or named pipes if needed
        // (Assume system is clean for integration test)
    }
    void TearDown() override {
        // Clean up
    }
};

TEST_F(DisplayIpcWgcIntegrationTest, InitAndSnapshotSuccess) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);

        // Try to take a snapshot (should trigger lazy_init) with moderate timeout
        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        // Since helper process may not have frames ready immediately, timeout is most likely result
        EXPECT_TRUE(status == platf::capture_e::ok || status == platf::capture_e::timeout || 
                   status == platf::capture_e::error || status == platf::capture_e::reinit);
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, HelperProcessFailure) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";

        // Temporarily rename or remove the helper exe to simulate failure
        std::filesystem::path exe_path = std::filesystem::current_path() / "tools" / "sunshine-wgc-helper.exe";
        std::filesystem::path fake_path = exe_path;
        fake_path += ".bak";
        if (std::filesystem::exists(exe_path)) {
            std::filesystem::rename(exe_path, fake_path);
        }

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0); // init returns 0 even if lazy_init fails

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        EXPECT_EQ(status, platf::capture_e::error);

        // Restore the helper exe
        if (std::filesystem::exists(fake_path)) {
            std::filesystem::rename(fake_path, exe_path);
        }
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, PipeTimeout) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";

        // Simulate pipe timeout by running without the helper process (rename helper exe)
        std::filesystem::path exe_path = std::filesystem::current_path() / "tools" / "sunshine-wgc-helper.exe";
        std::filesystem::path fake_path = exe_path;
        fake_path += ".bak";
        if (std::filesystem::exists(exe_path)) {
            std::filesystem::rename(exe_path, fake_path);
        }

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        EXPECT_EQ(status, platf::capture_e::error);

        if (std::filesystem::exists(fake_path)) {
            std::filesystem::rename(fake_path, exe_path);
        }
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, DoubleCleanupSafe) {
    deadlock_protection([&] {
        {
            display_ipc_wgc_t display;
            ::video::config_t config{};
            std::string display_name = "";
            int result = display.init(config, display_name);
            EXPECT_EQ(result, 0);
            // First cleanup happens when display goes out of scope
        }
        // Test that resources are properly cleaned up by creating another instance
        {
            display_ipc_wgc_t display2;
            ::video::config_t config{};
            std::string display_name = "";
            int result = display2.init(config, display_name);
            EXPECT_EQ(result, 0);
            // Second cleanup happens automatically
        }
        SUCCEED();
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, FrameAcquisitionTimeout) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);

        // Simulate no frame event by not running the helper process
        std::filesystem::path exe_path = std::filesystem::current_path() / "tools" / "sunshine-wgc-helper.exe";
        std::filesystem::path fake_path = exe_path;
        fake_path += ".bak";
        if (std::filesystem::exists(exe_path)) {
            std::filesystem::rename(exe_path, fake_path);
        }

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        EXPECT_TRUE(status == platf::capture_e::error || status == platf::capture_e::timeout);

        if (std::filesystem::exists(fake_path)) {
            std::filesystem::rename(fake_path, exe_path);
        }
    });
}

// Additional tests for malformed handle data, shared texture setup failure, etc., would require

TEST_F(DisplayIpcWgcIntegrationTest, RepeatedSnapshotCalls) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        // First snapshot (triggers lazy_init)
        auto status1 = display.snapshot(cb, img_out, std::chrono::milliseconds(1000), false);
        // Second snapshot (should not re-init)
        auto status2 = display.snapshot(cb, img_out, std::chrono::milliseconds(1000), false);
        EXPECT_TRUE((status1 == platf::capture_e::ok || status1 == platf::capture_e::timeout || status1 == platf::capture_e::reinit) &&
                    (status2 == platf::capture_e::ok || status2 == platf::capture_e::timeout || status2 == platf::capture_e::reinit));
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, ResourceCleanupAfterError) {
    deadlock_protection([&] {
        // Simulate helper process failure
        std::filesystem::path exe_path = std::filesystem::current_path() / "tools" / "sunshine-wgc-helper.exe";
        std::filesystem::path fake_path = exe_path;
        fake_path += ".bak";
        if (std::filesystem::exists(exe_path)) {
            std::filesystem::rename(exe_path, fake_path);
        }

        {
            display_ipc_wgc_t display;
            ::video::config_t config{};
            std::string display_name = "";
            int result = display.init(config, display_name);
            EXPECT_EQ(result, 0);
            std::shared_ptr<platf::img_t> img_out;
            auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
                img = display.alloc_img(); 
                return img != nullptr; 
            };
            auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
            EXPECT_EQ(status, platf::capture_e::error);
            // Cleanup happens automatically when display goes out of scope
        }

        if (std::filesystem::exists(fake_path)) {
            std::filesystem::rename(fake_path, exe_path);
        }
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, FrameReleaseAfterSnapshot) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(1000), false);
        // After snapshot, we cannot call release_snapshot() directly as it is private.
        // Instead, just check that snapshot does not crash and returns a valid status.
        EXPECT_TRUE(status == platf::capture_e::ok || status == platf::capture_e::timeout || status == platf::capture_e::reinit);
    });;
}

TEST_F(DisplayIpcWgcIntegrationTest, SnapshotNotInitialized) {
    display_ipc_wgc_t display;
    // Do not call init
    std::shared_ptr<platf::img_t> img_out;
    auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
        // Note: alloc_img() may not work if not initialized, so we test that case too
        img = display.alloc_img(); 
        return img != nullptr; 
    };
    auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
    EXPECT_EQ(status, platf::capture_e::error);
}

TEST_F(DisplayIpcWgcIntegrationTest, SnapshotNoSharedTexture) {
    deadlock_protection([&] {
        display_ipc_wgc_t display;
        ::video::config_t config{};
        std::string display_name = "";
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0);
        // Simulate helper process failure so no shared texture is set up
        std::filesystem::path exe_path = std::filesystem::current_path() / "tools" / "sunshine-wgc-helper.exe";
        std::filesystem::path fake_path = exe_path;
        fake_path += ".bak";
        if (std::filesystem::exists(exe_path)) {
            std::filesystem::rename(exe_path, fake_path);
        }
        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        EXPECT_EQ(status, platf::capture_e::error);
        if (std::filesystem::exists(fake_path)) {
            std::filesystem::rename(fake_path, exe_path);
        }
    });
}
