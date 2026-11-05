/**
 * @file tests/integration/xvfb_server.h
 * @brief Xvfb lifecycle helper for X11 and EGL integration tests.
 */
#pragma once

#ifdef __linux__
  // standard includes
  #include <array>
  #include <cerrno>
  #include <chrono>
  #include <csignal>
  #include <cstddef>
  #include <cstdlib>
  #include <format>
  #include <string>
  #include <thread>

  // platform includes
  #include <spawn.h>
  #include <sys/socket.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/un.h>
  #include <sys/wait.h>
  #include <unistd.h>

namespace xvfb {
  /**
   * @brief Result of trying to start the Xvfb server.
   */
  enum class start_result_e {
    started,  ///< The server is running.
    unavailable,  ///< Xvfb is not installed.
    failed  ///< Xvfb is installed but no display could be started.
  };

  /**
   * @brief Whether the integration environment is required to be present.
   *
   * Set SUNSHINE_REQUIRE_INTEGRATION_TESTS to turn the environment-related
   * skips into failures, so a runner can enforce that the tests really ran.
   *
   * @return True when the environment must be available.
   */
  inline bool integration_environment_required() {
    return std::getenv("SUNSHINE_REQUIRE_INTEGRATION_TESTS") != nullptr;
  }

  /**
   * @brief Whether the GL integration path is expected to be unreliable here.
   *
   * The ARM64 Alpine CI image crashes the software GL driver in the EGL upload
   * (a segfault on one run, a multi-hour hang on another), while the same
   * image passes on x86_64 and glibc ARM64 hosts run it natively. Alpine is
   * detected at compile time by musl not defining __GLIBC__.
   *
   * @return True when the GL upload cannot be trusted on this platform.
   */
  inline bool gl_integration_unreliable() {
  #if defined(__linux__) && !defined(__GLIBC__) && (defined(__aarch64__) || defined(__arm__))
    return true;
  #else
    return false;
  #endif
  }

  /**
   * @brief RAII Xvfb server used by display-related integration tests.
   *
   * Tests run sequentially and start one server at a time; the DISPLAY
   * override is process-global, so keeping several servers alive at once would
   * make the restoration order decide which display later tests see.
   */
  class xvfb_server_t {
  public:
    xvfb_server_t() = default;
    xvfb_server_t(const xvfb_server_t &) = delete;
    xvfb_server_t &operator=(const xvfb_server_t &) = delete;

    /**
     * @brief Stop the server and restore the previous DISPLAY value.
     */
    ~xvfb_server_t() {
      stop();
    }

    /**
     * @brief Start Xvfb on a private display number.
     *
     * @param width Screen width in pixels.
     * @param height Screen height in pixels.
     * @param depth Screen depth in bits per pixel.
     * @return Whether the server was started, is unavailable, or failed.
     */
    start_result_e start(int width = 1280, int height = 720, int depth = 24) {
      if (pid_ > 0) {
        // The running server keeps its geometry, so a different request must
        // fail loudly instead of testing against the wrong screen size.
        return width == width_ && height == height_ && depth == depth_ ? start_result_e::started : start_result_e::failed;
      }

      if (!x11_socket_directory_safe()) {
        return start_result_e::failed;
      }

      const char *previous_display = std::getenv("DISPLAY");
      previous_display_ = previous_display ? previous_display : "";

      for (int display = 90; display < 140; ++display) {
        const std::string display_name = std::format(":{}", display);
        const std::string socket = std::format("/tmp/.X11-unix/X{}", display);  // NOSONAR(cpp:S5443) - Xvfb creates this socket itself; x11_socket_directory_safe() validates the directory owner and type first
        // Only a live server owns the display; a stale socket or lock left by
        // a killed server is reclaimed by Xvfb itself (it validates the
        // lock's PID), so it must not skip the number.
        if (socket_connectable(socket)) {
          continue;
        }

        pid_t pid = -1;
        const int spawn_status = spawn_display(pid, display_name, width, height, depth);
        if (spawn_status == ENOENT) {
          return start_result_e::unavailable;
        }
        if (spawn_status != 0) {
          // Transient spawn failures should not abort the search.
          continue;
        }

        bool child_gone = false;
        if (wait_until_ready(pid, socket, child_gone)) {
          pid_ = pid;
          width_ = width;
          height_ = height;
          depth_ = depth;
          ::setenv("DISPLAY", display_name.c_str(), 1);
          display_overridden_ = true;
          return start_result_e::started;
        }

        if (!child_gone) {
          ::kill(pid, SIGTERM);
          wait_for_exit(pid);
        }
      }

      return start_result_e::failed;
    }

    /**
     * @brief Stop the server and restore the previous DISPLAY value.
     */
    void stop() {
      if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        wait_for_exit(pid_);
        pid_ = -1;
      }

      // Only restore DISPLAY when this object actually replaced it, so an
      // unused helper cannot wipe the environment of later tests.
      if (display_overridden_) {
        if (!previous_display_.empty()) {
          ::setenv("DISPLAY", previous_display_.c_str(), 1);
        } else {
          ::unsetenv("DISPLAY");
        }
        display_overridden_ = false;
      }
    }

  private:
    /**
     * @brief Check that the shared X11 socket directory is not attacker-controlled.
     *
     * Xvfb creates its socket under /tmp/.X11-unix, so a symlinked or
     * foreign-owned directory would let another user redirect the socket the
     * tests connect to. The directory must be a real directory owned by root
     * or the current user.
     *
     * @return True when the directory is missing (Xvfb creates it) or safe.
     */
    static bool x11_socket_directory_safe() {
      struct stat st {};
      if (::lstat("/tmp/.X11-unix", &st) != 0) {  // NOSONAR(cpp:S5443) - fixed X server path, only inspected to validate the directory
        // Xvfb creates the directory on first use when it is missing.
        return errno == ENOENT;
      }
      return S_ISDIR(st.st_mode) && (st.st_uid == 0 || st.st_uid == ::getuid());
    }

    /**
     * @brief Spawn Xvfb for a display name.
     *
     * @param pid Output process ID; -1 when spawning failed.
     * @param display_name X11 display name, e.g. ":90".
     * @param width Screen width in pixels.
     * @param height Screen height in pixels.
     * @param depth Screen depth in bits per pixel.
     * @return posix_spawn status; zero on success.
     */
    static int spawn_display(pid_t &pid, const std::string &display_name, int width, int height, int depth) {
      std::array<std::string, 7> args {
        "Xvfb",
        display_name,
        "-screen",
        "0",
        std::format("{}x{}x{}", width, height, depth),
        "-nolisten",
        "tcp",
      };
      std::array<char *, 8> argv {};
      for (std::size_t i = 0; i < args.size(); ++i) {
        argv[i] = args[i].data();
      }

      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
  #ifdef POSIX_SPAWN_SETSID
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
  #endif

      const int spawn_status = ::posix_spawnp(&pid, "Xvfb", nullptr, &attr, argv.data(), environ);
      posix_spawnattr_destroy(&attr);
      return spawn_status;
    }

    /**
     * @brief Wait until a spawned Xvfb accepts connections or exits.
     *
     * @param pid Xvfb process ID.
     * @param socket Socket path to probe.
     * @param child_gone Set to true when Xvfb exited before becoming ready.
     * @return True when the server is serving the display.
     */
    static bool wait_until_ready(pid_t pid, const std::string &socket, bool &child_gone) {
      for (int attempt = 0; attempt < 100; ++attempt) {
        // Bail out early when Xvfb failed to start (for example because the
        // server rejected the display).
        if (int child_status = 0; ::waitpid(pid, &child_status, WNOHANG) == pid) {
          child_gone = true;
          return false;
        }
        // Only a connectable socket proves the server is actually serving
        // this display; a stale socket file refuses the connection.
        if (socket_connectable(socket)) {
          return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      return false;
    }

    /**
     * @brief Check whether an X11 socket accepts connections.
     *
     * @param socket Socket path to probe.
     * @return True when a server is listening on the socket.
     */
    static bool socket_connectable(const std::string &socket) {
      const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd < 0) {
        return false;
      }

      struct sockaddr_un address {};
      address.sun_family = AF_UNIX;
      const auto path_end = std::format_to_n(address.sun_path, sizeof(address.sun_path) - 1, "{}", socket).out;
      *path_end = '\0';

      const bool connectable = ::connect(fd, static_cast<struct sockaddr *>(static_cast<void *>(&address)), sizeof(address)) == 0;
      ::close(fd);
      return connectable;
    }

    /**
     * @brief Reap a child, escalating to SIGKILL when it does not exit.
     *
     * @param pid Child process ID to reap.
     */
    static void wait_for_exit(pid_t pid) {
      for (int attempt = 0; attempt < 40; ++attempt) {
        if (::waitpid(pid, nullptr, WNOHANG) == pid) {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      ::kill(pid, SIGKILL);
      ::waitpid(pid, nullptr, 0);
    }

    pid_t pid_ {-1};  ///< Xvfb process ID.
    int width_ {0};  ///< Screen width of the running server.
    int height_ {0};  ///< Screen height of the running server.
    int depth_ {0};  ///< Screen depth of the running server.
    bool display_overridden_ {false};  ///< Whether this object replaced DISPLAY.
    std::string previous_display_;  ///< DISPLAY value to restore on stop.
  };
}  // namespace xvfb
#endif
