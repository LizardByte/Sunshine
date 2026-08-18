/**
 * @file src/platform/windows/ib_input_simulator.cpp
 * @brief Secure dynamic access to the optional IbInputSimulator runtime.
 */

// standard includes
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "ib_input_simulator.h"
#include "src/logging.h"

namespace platf::ib_input_simulator {
  using namespace std::literals;

  namespace {
    constexpr std::uint32_t SUCCESS = 0;  ///< IbInputSimulator success status.
    constexpr auto DLL_NAME = L"IbInputSimulator.dll"sv;  ///< Optional runtime filename.

    /**
     * @brief Human-readable backend label used in logs.
     *
     * @param selected Backend.
     * @return Backend label.
     */
    std::string_view backend_name(backend selected) {
      switch (selected) {
        case backend::logitech_ghub:
          return "Logitech G HUB"sv;
        case backend::razer:
          return "Razer"sv;
      }

      return "unknown"sv;
    }

    /**
     * @brief Resolve the absolute optional DLL path beside the running executable.
     *
     * @return Absolute DLL path, or an empty path when the executable path is unavailable.
     */
    std::filesystem::path dll_path() {
      std::vector<wchar_t> buffer(32768);
      const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
      if (length == 0 || length >= buffer.size()) {
        return {};
      }

      return std::filesystem::path {std::wstring_view {buffer.data(), length}}.parent_path() / DLL_NAME;
    }

    /**
     * @brief Convert an IbInputSimulator status code into a stable log label.
     *
     * @param error IbInputSimulator status code.
     * @return Human-readable status label.
     */
    std::string_view error_name(std::uint32_t error) {
      switch (error) {
        case 0:
          return "success"sv;
        case 1:
          return "invalid argument"sv;
        case 2:
          return "library not found"sv;
        case 3:
          return "library load failed"sv;
        case 4:
          return "library error"sv;
        case 5:
          return "device creation failed"sv;
        case 6:
          return "device not found"sv;
        case 7:
          return "device open failed"sv;
        default:
          return "unknown error"sv;
      }
    }

    /**
     * @brief Resolve one C ABI entry point from the loaded module.
     *
     * @tparam Function Function pointer type.
     * @param module Loaded module.
     * @param name Export name.
     * @return Resolved function pointer, or `nullptr`.
     */
    template<typename Function>
    Function resolve(HMODULE module, const char *name) {
      return reinterpret_cast<Function>(GetProcAddress(module, name));
    }
  }  // namespace

  runtime_t::runtime_t(HMODULE module, api_t api):
      module_ {module},
      api_ {api} {}

  std::optional<backend> backend_for_value(std::string_view value) {
    if (value == "logitech_ghub"sv) {
      return backend::logitech_ghub;
    }
    if (value == "razer"sv) {
      return backend::razer;
    }

    return std::nullopt;
  }

  std::unique_ptr<runtime_t> runtime_t::create(backend selected) {
    auto path = dll_path();
#ifdef SUNSHINE_TESTS
    const auto &override_path = dll_path_override();
    if (!override_path.empty()) {
      path = override_path;
    }
#endif
    if (path.empty()) {
      BOOST_LOG(warning) << "Unable to determine the Sunshine application directory for IbInputSimulator"sv;
      return nullptr;
    }

    const auto module = LoadLibraryExW(
      path.c_str(),
      nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32
    );
    if (!module) {
      BOOST_LOG(warning) << "Unable to load optional IbInputSimulator runtime from "sv << path.string() << " (Windows error "sv << GetLastError() << ')';
      return nullptr;
    }

    api_t api {
      .init = resolve<api_t::init_t>(module, "IbSendInit"),
      .destroy = resolve<api_t::destroy_t>(module, "IbSendDestroy"),
      .send_input = resolve<api_t::send_input_t>(module, "IbSendInput"),
    };
    if (!api.init || !api.destroy || !api.send_input) {
      BOOST_LOG(warning) << "IbInputSimulator.dll does not export the required C ABI"sv;
      FreeLibrary(module);
      return nullptr;
    }

    const auto error = api.init(static_cast<std::uint32_t>(selected), 0, nullptr);
    if (error != SUCCESS) {
      BOOST_LOG(warning) << "Unable to initialize the IbInputSimulator "sv << backend_name(selected) << " backend: "sv << error_name(error) << " ("sv << error << ')';
      FreeLibrary(module);
      return nullptr;
    }

    BOOST_LOG(info) << "IbInputSimulator "sv << backend_name(selected) << " backend initialized from "sv << path.string();
    return std::unique_ptr<runtime_t> {new runtime_t {module, api}};
  }

  runtime_t::~runtime_t() {
    {
      std::lock_guard lock {mutex_};
      if (initialized_) {
        api_.destroy();
        initialized_ = false;
      }
    }

    if (module_) {
      FreeLibrary(module_);
    }
  }

  bool runtime_t::send(std::span<const INPUT> inputs) {
    if (inputs.empty() || inputs.size() > std::numeric_limits<UINT>::max()) {
      return inputs.empty();
    }

    std::lock_guard lock {mutex_};
    if (!initialized_) {
      return false;
    }

    const auto count = static_cast<UINT>(inputs.size());
    return api_.send_input(count, const_cast<LPINPUT>(inputs.data()), sizeof(INPUT)) == count;
  }

  bool runtime_t::send(const INPUT &input) {
    return send(std::span {&input, 1});
  }

#ifdef SUNSHINE_TESTS
  std::unique_ptr<runtime_t> runtime_t::create_for_testing(api_t api) {
    if (!api.init || !api.destroy || !api.send_input) {
      return nullptr;
    }

    return std::unique_ptr<runtime_t> {new runtime_t {nullptr, api}};
  }

  std::filesystem::path &runtime_t::dll_path_override() {
    static std::filesystem::path path;
    return path;
  }

  void runtime_t::set_dll_path_override_for_testing(std::filesystem::path path) {
    dll_path_override() = std::move(path);
  }

  void runtime_t::clear_dll_path_override_for_testing() {
    dll_path_override().clear();
  }
#endif
}  // namespace platf::ib_input_simulator
