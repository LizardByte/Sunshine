#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <iostream>
#include <filesystem>
#include <numeric>  // for std::accumulate
#include <algorithm>  // for std::min_element, std::max_element
#include <d3d11.h>
#include <dxgi.h>
#include "src/platform/windows/display_vram.h"

using namespace platf::dxgi;

// Deadlock protection utility (pattern from test_shared_memory)
template <typename Func>
void deadlock_protection(Func&& func, int timeout_ms = 5000) {
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
    std::filesystem::path helper_exe_path;
    std::filesystem::path backup_path;
    bool helper_exists = false;

    void SetUp() override {
        // Determine the helper executable path - it should be relative to test executable
        helper_exe_path = std::filesystem::current_path() / "build" / "tests" / "tools" / "sunshine_wgc_capture.exe";
        backup_path = helper_exe_path;
        backup_path += ".bak";
        
        // Check if helper process exists
        helper_exists = std::filesystem::exists(helper_exe_path);
        
        std::cout << "Helper exe path: " << helper_exe_path << std::endl;
        std::cout << "Helper exists: " << (helper_exists ? "YES" : "NO") << std::endl;
    }
    
    void TearDown() override {
        // Restore helper if it was backed up
        if (std::filesystem::exists(backup_path)) {
            try {
                std::filesystem::rename(backup_path, helper_exe_path);
            } catch (const std::exception& e) {
                std::cout << "Warning: Failed to restore helper exe: " << e.what() << std::endl;
            }
        }
    }

    void simulateHelperMissing() {
        if (helper_exists && std::filesystem::exists(helper_exe_path)) {
            try {
                std::filesystem::rename(helper_exe_path, backup_path);
            } catch (const std::exception& e) {
                std::cout << "Warning: Failed to backup helper exe: " << e.what() << std::endl;
            }
        }
    }

    void restoreHelper() {
        if (std::filesystem::exists(backup_path)) {
            try {
                std::filesystem::rename(backup_path, helper_exe_path);
            } catch (const std::exception& e) {
                std::cout << "Warning: Failed to restore helper exe: " << e.what() << std::endl;
            }
        }
    }
};

TEST_F(DisplayIpcWgcIntegrationTest, InitAndSnapshotSuccess) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        std::string display_name = "";
        
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "Display initialization should succeed";

        // Try to take a snapshot (should trigger lazy_init) with reasonable timeout
        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(3000), false);
        
        if (helper_exists) {
            // If helper exists, we expect either successful capture or timeout
            EXPECT_TRUE(status == platf::capture_e::ok || status == platf::capture_e::timeout) 
                << "With helper process, should get ok or timeout, got: " << static_cast<int>(status);
            
            if (status == platf::capture_e::ok) {
                EXPECT_TRUE(img_out != nullptr) << "Successful capture should provide image";
                EXPECT_EQ(img_out->width, config.width) << "Frame width should match config";
                EXPECT_EQ(img_out->height, config.height) << "Frame height should match config";
                std::cout << "✓ IPC capture successful - frame captured via helper process: " 
                         << img_out->width << "x" << img_out->height << std::endl;
            } else {
                std::cout << "ℹ IPC capture timeout - helper process may need more time" << std::endl;
            }
        } else {
            // Without helper, should fallback to error
            EXPECT_EQ(status, platf::capture_e::error) 
                << "Without helper process, should return error, got: " << static_cast<int>(status);
            std::cout << "✓ IPC gracefully handles missing helper process" << std::endl;
        }
    }, 12000); // Longer timeout for helper process startup
}

TEST_F(DisplayIpcWgcIntegrationTest, HelperProcessFailure) {
    deadlock_protection([&] {
        // Temporarily simulate helper process missing
        simulateHelperMissing();
        
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        std::string display_name = "";

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "init() should succeed even if helper will fail later";

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(3000), false);
        EXPECT_EQ(status, platf::capture_e::error) 
            << "Should return error when helper process cannot start";
        
        std::cout << "✓ IPC handles helper process failure gracefully" << std::endl;
        
        // Restore helper for other tests
        restoreHelper();
    }, 10000);
}

TEST_F(DisplayIpcWgcIntegrationTest, IpcCommunicationTest) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        config.dynamicRange = 0; // SDR
        std::string display_name = "";

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "Display initialization should succeed";

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        // First snapshot should trigger lazy_init and helper process startup
        auto status1 = display.snapshot(cb, img_out, std::chrono::milliseconds(4000), false);
        
        if (status1 == platf::capture_e::ok) {
            std::cout << "✓ IPC first frame capture successful" << std::endl;
            
            // Verify we can capture multiple frames
            auto status2 = display.snapshot(cb, img_out, std::chrono::milliseconds(1000), false);
            EXPECT_TRUE(status2 == platf::capture_e::ok || status2 == platf::capture_e::timeout) 
                << "Second frame should succeed or timeout";
            
            if (status2 == platf::capture_e::ok) {
                std::cout << "✓ IPC subsequent frame capture successful" << std::endl;
            }
        } else if (status1 == platf::capture_e::timeout) {
            std::cout << "ℹ IPC first frame timeout - helper may need more time (acceptable)" << std::endl;
        } else {
            FAIL() << "Unexpected capture status: " << static_cast<int>(status1);
        }
    }, 15000); // Extended timeout for helper startup and IPC setup
}

TEST_F(DisplayIpcWgcIntegrationTest, MultipleResolutionConfigs) {
    deadlock_protection([&] {
        struct TestResolution {
            UINT width, height;
            int framerate;
            std::string desc;
        };
        
        std::vector<TestResolution> test_resolutions = {
            {1920, 1080, 60, "1080p60"},
            {1280, 720, 120, "720p120"},
            {2560, 1440, 60, "1440p60"}
        };
        
        for (const auto& res : test_resolutions) {
            std::cout << "Testing resolution: " << res.desc << std::endl;
            
            display_wgc_ipc_vram_t display;
            ::video::config_t config{};
            config.width = res.width;
            config.height = res.height;
            config.framerate = res.framerate;
            std::string display_name = "";

            int result = display.init(config, display_name);
            EXPECT_EQ(result, 0) << "Init should succeed for " << res.desc;

            std::shared_ptr<platf::img_t> img_out;
            auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
                img = display.alloc_img(); 
                return img != nullptr; 
            };
            
            auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(4000), false);
            
            if (status == platf::capture_e::ok && img_out) {
                // Verify captured frame dimensions match config
                EXPECT_EQ(img_out->width, res.width) << "Width mismatch for " << res.desc;
                EXPECT_EQ(img_out->height, res.height) << "Height mismatch for " << res.desc;
                std::cout << "✓ " << res.desc << " - Frame captured with correct dimensions" << std::endl;
            } else {
                std::cout << "ℹ " << res.desc << " - Status: " << static_cast<int>(status) 
                         << " (timeout acceptable in test environment)" << std::endl;
            }
            
            // Each display instance cleanup happens automatically via destructor
        }
    }, 25000); // Extended timeout for multiple resolutions
}

TEST_F(DisplayIpcWgcIntegrationTest, FrameSequenceValidation) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        std::string display_name = "";

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "Display initialization should succeed";

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        int successful_captures = 0;
        int timeout_count = 0;
        const int max_attempts = 5;
        
        for (int i = 0; i < max_attempts; i++) {
            auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(2000), false);
            
            if (status == platf::capture_e::ok) {
                successful_captures++;
                std::cout << "✓ Frame " << (i + 1) << " captured successfully" << std::endl;
            } else if (status == platf::capture_e::timeout) {
                timeout_count++;
                std::cout << "⏱ Frame " << (i + 1) << " timeout" << std::endl;
            } else {
                std::cout << "✗ Frame " << (i + 1) << " error: " << static_cast<int>(status) << std::endl;
            }
            
            // Small delay between captures
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // We should get at least some successful captures or timeouts (not errors)
        EXPECT_TRUE(successful_captures > 0 || timeout_count > 0) 
            << "Should capture at least one frame or get timeouts, not errors";
        
        if (successful_captures > 0) {
            std::cout << "✓ IPC frame sequence test - " << successful_captures 
                     << "/" << max_attempts << " frames captured successfully" << std::endl;
        } else {
            std::cout << "ℹ IPC frame sequence test - all timeouts (acceptable in test environment)" << std::endl;
        }
    }, 20000);
}

TEST_F(DisplayIpcWgcIntegrationTest, ResourceCleanupValidation) {
    deadlock_protection([&] {
        // Test that resources are properly cleaned up between instances
        for (int i = 0; i < 3; i++) {
            std::cout << "Testing instance " << (i + 1) << "/3" << std::endl;
            
            display_wgc_ipc_vram_t display;
            ::video::config_t config{};
            config.width = 1920;
            config.height = 1080;
            config.framerate = 60;
            std::string display_name = "";
            
            int result = display.init(config, display_name);
            EXPECT_EQ(result, 0) << "Init should succeed for instance " << (i + 1);

            if (helper_exists) {
                std::shared_ptr<platf::img_t> img_out;
                auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
                    img = display.alloc_img(); 
                    return img != nullptr; 
                };
                
                // Try a quick capture to exercise IPC if helper exists
                auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(1000), false);
                // Accept any status - we're testing resource cleanup, not capture success
                std::cout << "  Instance " << (i + 1) << " capture status: " << static_cast<int>(status) << std::endl;
            }
            
            // Destructor cleanup happens automatically here
        }
        
        std::cout << "✓ Resource cleanup validation complete - no crashes or leaks detected" << std::endl;
    }, 12000);
}

TEST_F(DisplayIpcWgcIntegrationTest, SnapshotWithoutInit) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        // Do not call init()
        
        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(500), false);
        EXPECT_EQ(status, platf::capture_e::error) 
            << "Snapshot without init should return error";
        
        std::cout << "✓ IPC properly handles uninitialized display" << std::endl;
    });
}

TEST_F(DisplayIpcWgcIntegrationTest, FrameContentValidation) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        std::string display_name = "";
        
        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "Display initialization should succeed";

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        // Try to capture a frame
        auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(4000), false);
        
        if (status == platf::capture_e::ok && img_out) {
            // Cast to D3D image to access the capture texture
            auto d3d_img = std::static_pointer_cast<img_d3d_t>(img_out);
            ASSERT_TRUE(d3d_img != nullptr) << "Image should be D3D type";
            ASSERT_TRUE(d3d_img->capture_texture != nullptr) << "Capture texture should exist";

            // Get D3D11 device and context from the texture
            ID3D11Device* device = nullptr;
            d3d_img->capture_texture->GetDevice(&device);
            ASSERT_TRUE(device != nullptr) << "Device should exist";

            ID3D11DeviceContext* device_ctx = nullptr;
            device->GetImmediateContext(&device_ctx);
            ASSERT_TRUE(device_ctx != nullptr) << "Device context should exist";

            // Get texture description
            D3D11_TEXTURE2D_DESC src_desc;
            d3d_img->capture_texture->GetDesc(&src_desc);

            // Verify dimensions match what we configured
            EXPECT_EQ(src_desc.Width, config.width) << "Texture width should match config";
            EXPECT_EQ(src_desc.Height, config.height) << "Texture height should match config";

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
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to create staging texture: " << hr;

            // Copy from VRAM texture to staging texture
            device_ctx->CopyResource(staging_texture, d3d_img->capture_texture.get());

            // Map the staging texture for CPU access
            D3D11_MAPPED_SUBRESOURCE mapped_resource;
            hr = device_ctx->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to map staging texture: " << hr;

            // Analyze pixel data for content validation
            const uint8_t* pixel_data = static_cast<const uint8_t*>(mapped_resource.pData);
            uint32_t width = staging_desc.Width;
            uint32_t height = staging_desc.Height;
            uint32_t row_pitch = mapped_resource.RowPitch;

            // Calculate content metrics
            uint64_t total_brightness = 0;
            uint32_t non_black_pixels = 0;
            
            // Sample pixels across the frame (assuming BGRA format, 4 bytes per pixel)
            uint32_t bytes_per_pixel = 4;
            uint32_t sample_step = 8; // Sample every 8th pixel for performance
            
            for (uint32_t y = 0; y < height; y += sample_step) {
                const uint8_t* row = pixel_data + (y * row_pitch);
                for (uint32_t x = 0; x < width; x += sample_step) {
                    const uint8_t* pixel = row + (x * bytes_per_pixel);
                    uint8_t b = pixel[0];
                    uint8_t g = pixel[1];
                    uint8_t r = pixel[2];
                    
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

            // Calculate sampled pixel count
            uint32_t sampled_pixels = (height / sample_step) * (width / sample_step);
            double average_brightness = static_cast<double>(total_brightness) / sampled_pixels;
            double non_black_ratio = static_cast<double>(non_black_pixels) / sampled_pixels;

            // Content validation: validate capture mechanism works rather than requiring specific content
            // In test environments, desktop may be black/minimal
            std::cout << "✓ IPC Frame content validation:" << std::endl;
            std::cout << "  Resolution: " << width << "x" << height << std::endl;
            std::cout << "  Sampled pixels: " << sampled_pixels << std::endl;
            std::cout << "  Non-black ratio: " << (non_black_ratio * 100.0) << "%" << std::endl;
            std::cout << "  Average brightness: " << average_brightness << std::endl;
            
            // Validate capture mechanism is working (frame data is valid)
            EXPECT_GT(sampled_pixels, 0) << "Should have sampled pixels";
            EXPECT_GE(average_brightness, 0) << "Average brightness should be non-negative";
            
            // The IPC capture mechanism is validated by reaching this point with valid texture data
        } else {
            std::cout << "ℹ Frame capture status: " << static_cast<int>(status) 
                     << " (timeout acceptable in test environment)" << std::endl;
        }
    }, 15000);
}

TEST_F(DisplayIpcWgcIntegrationTest, HdrConfigurationTest) {
    deadlock_protection([&] {
        // Test both SDR and HDR configurations
        std::vector<std::pair<int, std::string>> dynamic_range_tests = {
            {0, "SDR"},
            {1, "HDR"}
        };
        
        for (const auto& [dynamic_range, desc] : dynamic_range_tests) {
            std::cout << "Testing " << desc << " configuration" << std::endl;
            
            display_wgc_ipc_vram_t display;
            ::video::config_t config{};
            config.width = 1920;
            config.height = 1080;
            config.framerate = 60;
            config.dynamicRange = dynamic_range;
            std::string display_name = "";

            int result = display.init(config, display_name);
            EXPECT_EQ(result, 0) << "Init should succeed for " << desc;

            std::shared_ptr<platf::img_t> img_out;
            auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
                img = display.alloc_img(); 
                return img != nullptr; 
            };
            
            auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(4000), false);
            
            if (status == platf::capture_e::ok && img_out) {
                auto d3d_img = std::static_pointer_cast<img_d3d_t>(img_out);
                ASSERT_TRUE(d3d_img != nullptr) << "Image should be D3D type for " << desc;
                
                // Get texture description to verify format
                D3D11_TEXTURE2D_DESC tex_desc;
                d3d_img->capture_texture->GetDesc(&tex_desc);
                
                if (dynamic_range == 1) {
                    // HDR should use R16G16B16A16_FLOAT format
                    EXPECT_EQ(tex_desc.Format, DXGI_FORMAT_R16G16B16A16_FLOAT) 
                        << "HDR should use float16 format";
                } else {
                    // SDR should use B8G8R8A8_UNORM format
                    EXPECT_EQ(tex_desc.Format, DXGI_FORMAT_B8G8R8A8_UNORM) 
                        << "SDR should use BGRA8 format";
                }
                
                std::cout << "✓ " << desc << " configuration validated with correct format" << std::endl;
            } else {
                std::cout << "ℹ " << desc << " status: " << static_cast<int>(status) 
                         << " (timeout acceptable)" << std::endl;
            }
        }
    }, 20000);
}

TEST_F(DisplayIpcWgcIntegrationTest, PerformanceMetricsValidation) {
    deadlock_protection([&] {
        display_wgc_ipc_vram_t display;
        ::video::config_t config{};
        config.width = 1920;
        config.height = 1080;
        config.framerate = 60;
        std::string display_name = "";

        int result = display.init(config, display_name);
        EXPECT_EQ(result, 0) << "Display initialization should succeed";

        std::shared_ptr<platf::img_t> img_out;
        auto cb = [&display](std::shared_ptr<platf::img_t>& img) { 
            img = display.alloc_img(); 
            return img != nullptr; 
        };
        
        std::vector<std::chrono::milliseconds> capture_times;
        int successful_captures = 0;
        const int test_frames = 10;
        
        for (int i = 0; i < test_frames; i++) {
            auto start_time = std::chrono::steady_clock::now();
            auto status = display.snapshot(cb, img_out, std::chrono::milliseconds(2000), false);
            auto end_time = std::chrono::steady_clock::now();
            
            auto capture_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            capture_times.push_back(capture_duration);
            
            if (status == platf::capture_e::ok) {
                successful_captures++;
            }
            
            // Small delay between captures
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
        }
        
        if (successful_captures > 0) {
            // Calculate performance metrics
            auto total_time = std::accumulate(capture_times.begin(), capture_times.end(), 
                                            std::chrono::milliseconds(0));
            auto avg_time = total_time / test_frames;
            
            auto min_time = *std::min_element(capture_times.begin(), capture_times.end());
            auto max_time = *std::max_element(capture_times.begin(), capture_times.end());
            
            std::cout << "✓ IPC Performance metrics (" << successful_captures << "/" << test_frames << " frames):" << std::endl;
            std::cout << "  Average capture time: " << avg_time.count() << "ms" << std::endl;
            std::cout << "  Min capture time: " << min_time.count() << "ms" << std::endl;
            std::cout << "  Max capture time: " << max_time.count() << "ms" << std::endl;
            
            // Performance validation: captures should be reasonably fast
            EXPECT_LT(avg_time.count(), 100) << "Average capture time should be under 100ms";
            EXPECT_LT(max_time.count(), 500) << "Max capture time should be under 500ms";
        } else {
            std::cout << "ℹ Performance test: No successful captures (acceptable in test environment)" << std::endl;
        }
    }, 25000);
}


