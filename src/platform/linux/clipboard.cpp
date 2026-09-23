/**
 * @file src/platform/linux/clipboard.cpp
 * @brief X11 CLIPBOARD sync for a Sunshine session.
 */
#include "src/logging.h"
#include "src/platform/common.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#ifndef SUNSHINE_BUILD_X11

namespace platf {
  void clipboard_set(std::string_view) {
    // This build has no X11 display, so the host clipboard cannot be updated.
  }

  void clipboard_subscribe(clipboard_queue_t) {
    // This build has no X11 display, so host clipboard changes cannot be watched.
  }
}  // namespace platf

#else

  #include <fcntl.h>
  #include <poll.h>
  #include <unistd.h>
  #include <X11/extensions/Xfixes.h>
  #include <X11/Xatom.h>
  #include <X11/Xlib.h>

namespace platf {
  namespace {
    constexpr std::size_t k_max_bytes = 32755;

    struct state_t {
      std::mutex mutex;
      std::condition_variable ready_cv;
      bool ready = false;
      bool failed = false;
      bool started = false;
      bool subscribed = false;
      bool has_pending = false;
      bool pull = false;
      int wake_write = -1;
      std::string pending;
      std::string last_remote;
      clipboard_queue_t queue;
      std::thread thread;
    };

    state_t &state() {
      static state_t value;
      return value;
    }

    void publish(const std::string &text) {
      clipboard_queue_t queue;
      {
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        if (!clipboard.subscribed || text == clipboard.last_remote) {
          return;
        }
        clipboard.last_remote = text;
        queue = clipboard.queue;
      }
      if (!queue) {
        return;
      }

      std::random_device seed;
      auto token = seed();
      if (token == 0) {
        token = 1;
      }
      queue->raise(clipboard_text_t {token, text});
    }

    void serve_selection(Display *display, const std::string &text, XSelectionRequestEvent *request) {
      XEvent notify {};
      notify.xselection.type = SelectionNotify;
      notify.xselection.display = request->display;
      notify.xselection.requestor = request->requestor;
      notify.xselection.selection = request->selection;
      notify.xselection.target = request->target;
      notify.xselection.property = None;
      notify.xselection.time = request->time;

      auto utf8 = XInternAtom(display, "UTF8_STRING", False);
      auto targets = XInternAtom(display, "TARGETS", False);
      if (request->target == targets) {
        Atom list[3] = {utf8, XA_STRING, targets};
        XChangeProperty(display, request->requestor, request->property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(list), 3);
        notify.xselection.property = request->property;
      } else if (request->target == utf8 || request->target == XA_STRING) {
        XChangeProperty(display, request->requestor, request->property, request->target, 8, PropModeReplace, reinterpret_cast<const unsigned char *>(text.data()), static_cast<int>(text.size()));
        notify.xselection.property = request->property;
      }

      XSendEvent(display, request->requestor, False, 0, &notify);
    }

    void thread_main() {
      int wake_read = -1;
      int wake_write = -1;
      int pipes[2] = {-1, -1};
      if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) != 0) {
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        clipboard.failed = true;
        clipboard.ready_cv.notify_all();
        return;
      }
      wake_read = pipes[0];
      wake_write = pipes[1];

      Display *display = XOpenDisplay(nullptr);
      if (!display) {
        BOOST_LOG(warning) << "Clipboard sync could not open the X display"sv;
        close(wake_read);
        close(wake_write);
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        clipboard.failed = true;
        clipboard.ready_cv.notify_all();
        return;
      }

      int event_base = 0;
      int error_base = 0;
      if (!XFixesQueryExtension(display, &event_base, &error_base)) {
        BOOST_LOG(warning) << "Clipboard sync requires the XFixes extension"sv;
        XCloseDisplay(display);
        close(wake_read);
        close(wake_write);
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        clipboard.failed = true;
        clipboard.ready_cv.notify_all();
        return;
      }

      auto root = DefaultRootWindow(display);
      auto window = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
      auto clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
      auto utf8 = XInternAtom(display, "UTF8_STRING", False);
      auto property = XInternAtom(display, "SUNSHINE_CLIPBOARD", False);
      XFixesSelectSelectionInput(display, window, clipboard_atom, XFixesSetSelectionOwnerNotifyMask);
      XFlush(display);

      std::string owned;
      {
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        clipboard.wake_write = wake_write;
        clipboard.ready = true;
        clipboard.ready_cv.notify_all();
      }

      auto apply_pending = [&]() {
        std::string text;
        {
          auto &clipboard = state();
          std::lock_guard lock {clipboard.mutex};
          if (!clipboard.has_pending) {
            return;
          }
          text = std::move(clipboard.pending);
          clipboard.has_pending = false;
        }
        if (text.size() > k_max_bytes) {
          text.resize(k_max_bytes);
        }
        owned = text;
        XSetSelectionOwner(display, clipboard_atom, window, CurrentTime);
        XFlush(display);
      };

      auto read_selection = [&]() {
        Atom actual_type;
        int actual_format = 0;
        unsigned long item_count = 0;
        unsigned long bytes_after = 0;
        unsigned char *data = nullptr;
        if (XGetWindowProperty(display, window, property, 0, (k_max_bytes + 3) / 4, True, AnyPropertyType, &actual_type, &actual_format, &item_count, &bytes_after, &data) != Success || data == nullptr) {
          return;
        }
        std::string text(reinterpret_cast<char *>(data), item_count);
        XFree(data);
        if (text.size() > k_max_bytes) {
          text.resize(k_max_bytes);
        }
        publish(text);
      };

      int xfd = ConnectionNumber(display);
      while (true) {
        while (XPending(display)) {
          XEvent event;
          XNextEvent(display, &event);
          if (event.type == SelectionRequest) {
            serve_selection(display, owned, &event.xselectionrequest);
            XFlush(display);
          } else if (event.type == SelectionNotify) {
            read_selection();
          } else if (event.type == event_base + XFixesSelectionNotify) {
            auto *selection = reinterpret_cast<XFixesSelectionNotifyEvent *>(&event);
            if (selection->selection == clipboard_atom && selection->owner != window && selection->owner != None) {
              XConvertSelection(display, clipboard_atom, utf8, property, window, CurrentTime);
              XFlush(display);
            }
          }
        }

        apply_pending();

        bool pull = false;
        {
          auto &clipboard = state();
          std::lock_guard lock {clipboard.mutex};
          pull = clipboard.pull;
          clipboard.pull = false;
        }
        if (pull) {
          XConvertSelection(display, clipboard_atom, utf8, property, window, CurrentTime);
          XFlush(display);
        }

        pollfd fds[2] = {
          {xfd, POLLIN, 0},
          {wake_read, POLLIN, 0},
        };
        poll(fds, 2, 200);
        if (fds[1].revents & POLLIN) {
          char buffer[64];
          while (read(wake_read, buffer, sizeof(buffer)) > 0) {
          }
        }
      }
    }
  }  // namespace

  void clipboard_set(std::string_view text) {
    auto &clipboard = state();
    std::lock_guard lock {clipboard.mutex};
    if (clipboard.failed || !clipboard.ready) {
      return;
    }
    clipboard.pending.assign(text.data(), text.size());
    clipboard.last_remote = clipboard.pending;
    clipboard.has_pending = true;
    if (clipboard.wake_write >= 0) {
      char byte = 1;
      static_cast<void>(write(clipboard.wake_write, &byte, 1));
    }
  }

  void clipboard_subscribe(clipboard_queue_t queue) {
    auto &clipboard = state();
    std::unique_lock lock {clipboard.mutex};
    if (!clipboard.started) {
      clipboard.started = true;
      clipboard.thread = std::thread(thread_main);
      clipboard.thread.detach();
    }
    clipboard.ready_cv.wait(lock, [&]() {
      return clipboard.ready || clipboard.failed;
    });
    clipboard.queue = std::move(queue);
    clipboard.subscribed = clipboard.ready;
    clipboard.pull = clipboard.ready;
    if (clipboard.wake_write >= 0) {
      char byte = 1;
      static_cast<void>(write(clipboard.wake_write, &byte, 1));
    }
  }
}  // namespace platf

#endif
