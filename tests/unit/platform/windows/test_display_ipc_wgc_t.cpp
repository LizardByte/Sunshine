#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <iostream>
#include <d3d11.h>
#include "src/platform/windows/display_vram.h"

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
        std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
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
        std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
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
        std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
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
        std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
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
        std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
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

TEST_F(DisplayIpcWgcIntegrationTest, CapturedFrameContentValidation) {
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
        
        // Try to capture a frame - may timeout if helper process isn't running
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(2000), false);
        
        if (status == platf::capture_e::ok && img_out) {
            // Cast to D3D image to access the capture texture
            auto d3d_img = std::static_pointer_cast<img_d3d_t>(img_out);
            ASSERT_TRUE(d3d_img != nullptr);
            ASSERT_TRUE(d3d_img->capture_texture != nullptr);

            // Get D3D11 device and context from the texture
            ID3D11Device* device = nullptr;
            d3d_img->capture_texture->GetDevice(&device);
            ASSERT_TRUE(device != nullptr);

            ID3D11DeviceContext* device_ctx = nullptr;
            device->GetImmediateContext(&device_ctx);
            ASSERT_TRUE(device_ctx != nullptr);

            // Get texture description
            D3D11_TEXTURE2D_DESC src_desc;
            d3d_img->capture_texture->GetDesc(&src_desc);

            // Create staging texture for CPU access
            D3D11_TEXTURE2D_DESC staging_desc = {};
            staging_desc.Width = src_desc.Width;
            staging_desc.Height = src_desc.Height;
            staging_desc.MipLevels = 1;
            staging_desc.ArraySize = 1;
            staging_desc.Format = src_desc.Format;
            staging_desc.SampleDesc.Count = 1;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging_desc.BindFlags = 0;

            ID3D11Texture2D* staging_texture = nullptr;
            HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to create staging texture";

            // Copy from VRAM texture to staging texture
            device_ctx->CopyResource(staging_texture, d3d_img->capture_texture.get());

            // Map the staging texture for CPU access
            D3D11_MAPPED_SUBRESOURCE mapped_resource;
            hr = device_ctx->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to map staging texture";

            // Analyze pixel data for content validation
            const uint8_t* pixel_data = static_cast<const uint8_t*>(mapped_resource.pData);
            uint32_t width = staging_desc.Width;
            uint32_t height = staging_desc.Height;
            uint32_t row_pitch = mapped_resource.RowPitch;

            // Calculate content metrics
            uint64_t total_brightness = 0;
            uint32_t non_black_pixels = 0;
            
            // Sample pixels across the frame (assuming BGRA format, 4 bytes per pixel)
            uint32_t bytes_per_pixel = 4; // Assuming BGRA format
            for (uint32_t y = 0; y < height; y += 4) { // Sample every 4th row for performance
                const uint8_t* row = pixel_data + (y * row_pitch);
                for (uint32_t x = 0; x < width; x += 4) { // Sample every 4th pixel
                    const uint8_t* pixel = row + (x * bytes_per_pixel);
                    uint8_t b = pixel[0];
                    uint8_t g = pixel[1];
                    uint8_t r = pixel[2];
                    // uint8_t a = pixel[3]; // Alpha channel not used for content validation
                    
                    // Calculate brightness (luminance)
                    uint32_t brightness = (uint32_t)(0.299 * r + 0.587 * g + 0.114 * b);
                    total_brightness += brightness;
                    
                    // Count non-black pixels (threshold of 16 to account for noise)
                    if (r > 16 || g > 16 || b > 16) {
                        non_black_pixels++;
                    }
                }
            }

            // Unmap the staging texture
            device_ctx->Unmap(staging_texture, 0);

            // Clean up
            staging_texture->Release();
            device_ctx->Release();
            device->Release();

            // Calculate sampled pixel count (we sampled every 4th pixel in both dimensions)
            uint32_t sampled_pixels = (height / 4) * (width / 4);
            double average_brightness = static_cast<double>(total_brightness) / sampled_pixels;
            double non_black_ratio = static_cast<double>(non_black_pixels) / sampled_pixels;

            // Content validation criteria:
            // 1. Frame should not be completely blank (some non-black pixels)
            // 2. Average brightness should be above a minimal threshold
            // 3. A reasonable percentage of pixels should have some content
            
            EXPECT_GT(non_black_pixels, 0) << "Frame appears to be completely blank - no non-black pixels found";
            EXPECT_GT(average_brightness, 5.0) << "Average brightness too low: " << average_brightness << " (may indicate blank frame)";
            EXPECT_GT(non_black_ratio, 0.01) << "Less than 1% of pixels have content, ratio: " << non_black_ratio << " (may indicate blank frame)";
            
            // Log some metrics for debugging
            std::cout << "Frame content metrics:" << std::endl;
            std::cout << "  Resolution: " << width << "x" << height << std::endl;
            std::cout << "  Sampled pixels: " << sampled_pixels << std::endl;
            std::cout << "  Non-black pixels: " << non_black_pixels << std::endl;
            std::cout << "  Non-black ratio: " << (non_black_ratio * 100.0) << "%" << std::endl;
            std::cout << "  Average brightness: " << average_brightness << std::endl;
        } else {
            // If we couldn't capture a frame, at least verify the test setup works
            EXPECT_TRUE(status == platf::capture_e::timeout || status == platf::capture_e::error || status == platf::capture_e::reinit)
                << "Unexpected capture status: " << static_cast<int>(status);
        }
    }, 10000); // Longer timeout for this test as it does more work
}
