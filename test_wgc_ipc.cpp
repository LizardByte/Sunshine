// Simple test program to verify WGC IPC implementation
#include <iostream>
#include <thread>
#include <chrono>
#include "src/platform/windows/wgc/display_ipc_wgc_t.cpp"

int main() {
    std::wcout << L"Testing WGC IPC implementation..." << std::endl;
    
    // Create display instance
    platf::dxgi::display_ipc_wgc_t display;
    
    // Initialize with dummy config
    video::config_t config = {};
    config.dynamicRange = false;
    
    std::string display_name = "primary";
    
    int result = display.init(config, display_name);
    if (result != 0) {
        std::wcerr << L"Failed to initialize display: " << result << std::endl;
        return 1;
    }
    
    std::wcout << L"Display initialized successfully!" << std::endl;
    
    // Wait a bit to let everything settle
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::wcout << L"Test completed." << std::endl;
    return 0;
}
