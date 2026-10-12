/**
 * @file src/platform/linux/clipboard.cpp
 * @brief X11 CLIPBOARD sync for a Sunshine session.
 */
#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef SUNSHINE_BUILD_X11

namespace platf {
  void clipboard_set(std::string_view, const clipboard_queue_t &) {
    // This build has no X11 display, so the host clipboard cannot be updated.
  }

  void clipboard_subscribe(clipboard_queue_t) {
    // This build has no X11 display, so host clipboard changes cannot be watched.
  }

  void clipboard_unsubscribe(const clipboard_queue_t &) {
    // This build has no X11 display, so there is no clipboard subscription to remove.
  }
}  // namespace platf

#else

  #include "src/platform/clipboard_sync.h"

  #include <fcntl.h>
  #include <poll.h>
  #include <unistd.h>
  #include <X11/extensions/Xfixes.h>
  #include <X11/Xatom.h>
  #include <X11/Xlib.h>

namespace platf {
  namespace {
    /**
     * @brief Process-wide X11 clipboard watcher state.
     */
    struct state_t {
      std::mutex mutex;  ///< Guards the fields below for the input and X11 threads.
      std::condition_variable ready_cv;  ///< Wakes subscribers waiting for the display connection.
      bool ready = false;  ///< Whether the X11 watcher can accept clipboard operations.
      bool failed = false;  ///< Whether opening the display or XFixes failed.
      bool started = false;  ///< Whether the watcher thread has been launched.
      bool has_pending = false;  ///< Whether a client clipboard value is waiting to take selection ownership.
      bool pull = false;  ///< Whether a new subscriber still needs the current host clipboard.
      int wake_write = -1;  ///< Write end of the pipe that wakes the watcher thread.
      std::string pending;  ///< Client clipboard text waiting to become the selection owner.
      clipboard_subscribers_t subscribers;  ///< One entry for each streaming client that opted in.
      std::thread thread;  ///< Detached X11 watcher thread.
    };

    /**
     * @brief Return the process-wide clipboard watcher state.
     *
     * @return Watcher state shared by subscribe, unsubscribe, and the X11 thread.
     */
    state_t &state() {
      static state_t value;
      return value;
    }

    /**
     * @brief Raise clipboard text on each queue that still needs it.
     *
     * @param queues Subscriber queues selected by the publish or local-set rules.
     * @param text UTF-8 clipboard text.
     */
    void raise_clipboard(const std::vector<clipboard_queue_t> &queues, const std::string &text) {
      if (queues.empty()) {
        return;
      }

      std::random_device seed;
      for (const auto &queue : queues) {
        auto token = seed();
        if (token == 0) {
          token = 1;
        }
        queue->raise(clipboard_text_t {token, text});
      }
    }

    /**
     * @brief Publish host clipboard text to every subscriber that does not already have it.
     *
     * @param text UTF-8 clipboard text.
     */
    void publish(const std::string &text) {
      std::vector<clipboard_queue_t> queues;
      {
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        queues = clipboard.subscribers.publish(text);
      }
      raise_clipboard(queues, text);
    }

    /**
     * @brief Answer one X selection request with the text Sunshine owns.
     *
     * @param display X display connection owned by the watcher thread.
     * @param text UTF-8 text currently owned by Sunshine.
     * @param request Selection request from another client.
     * @param utf8 UTF8_STRING atom.
     * @param targets TARGETS atom.
     */
    void serve_selection(Display *display, const std::string &text, XSelectionRequestEvent *request, Atom utf8, Atom targets) {
      XEvent notify {};
      notify.xselection.type = SelectionNotify;
      notify.xselection.display = request->display;
      notify.xselection.requestor = request->requestor;
      notify.xselection.selection = request->selection;
      notify.xselection.target = request->target;
      notify.xselection.property = None;
      notify.xselection.time = request->time;

      const bool supported = request->target == targets || request->target == utf8 || request->target == XA_STRING;
      const auto property = static_cast<Atom>(selection_reply_property(request->property, request->target, supported));
      notify.xselection.property = property;
      if (property != None && request->target == targets) {
        Atom list[3] = {utf8, XA_STRING, targets};
        XChangeProperty(display, request->requestor, property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(list), 3);
      } else if (property != None) {
        XChangeProperty(
          display,
          request->requestor,
          property,
          request->target,
          8,
          PropModeReplace,
          reinterpret_cast<const unsigned char *>(text.data()),
          static_cast<int>(text.size())
        );
      }

      XSendEvent(display, request->requestor, False, 0, &notify);
    }

    /**
     * @brief Watch the X11 CLIPBOARD selection until the process exits.
     */
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
      XSelectInput(display, window, PropertyChangeMask);
      auto clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
      auto utf8 = XInternAtom(display, "UTF8_STRING", False);
      auto incr = XInternAtom(display, "INCR", False);
      auto targets = XInternAtom(display, "TARGETS", False);
      auto property = XInternAtom(display, "SUNSHINE_CLIPBOARD", False);
      XFixesSelectSelectionInput(display, window, clipboard_atom, XFixesSetSelectionOwnerNotifyMask);
      XFlush(display);

      std::string owned;
      incr_transfer_t transfer;
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
        owned = text;
        XSetSelectionOwner(display, clipboard_atom, window, CurrentTime);
        XFlush(display);
      };

      auto read_property = [&]() {
        const auto loaded = load_selection_property([display, window, property](unsigned long offset, unsigned long length) {
          selection_fragment_t fragment;
          Atom actual_type = None;
          int actual_format = 0;
          unsigned long item_count = 0;
          unsigned long bytes_after = 0;
          unsigned char *data = nullptr;
          if (XGetWindowProperty(display, window, property, offset, length, True, AnyPropertyType, &actual_type, &actual_format, &item_count, &bytes_after, &data) != Success) {
            if (data != nullptr) {
              XFree(data);
            }
            return fragment;
          }
          fragment.ok = true;
          fragment.type = actual_type;
          fragment.format = actual_format;
          fragment.item_count = item_count;
          fragment.bytes_after = bytes_after;
          if (data != nullptr) {
            const auto limit = static_cast<std::size_t>(length) * 4;
            const auto reported = item_count * property_byte_width(actual_format);
            const auto nbytes = std::min(reported, limit);
            fragment.data.resize(nbytes);
            if (nbytes > 0) {
              std::copy_n(reinterpret_cast<const std::byte *>(data), nbytes, fragment.data.begin());
            }
            XFree(data);
          }
          return fragment;
        });
        if (!loaded) {
          return;
        }
        if (!loaded->acknowledged) {
          XDeleteProperty(display, window, property);
        }
        const auto width = property_byte_width(loaded->format);
        const auto items = loaded->data.size() / width;
        const auto *bytes = loaded->data.empty() ? nullptr : loaded->data.data();
        auto text = transfer.consume(loaded->type, loaded->format, items, bytes, utf8, XA_STRING, incr);
        if (text) {
          publish(*text);
        }
      };

      int xfd = ConnectionNumber(display);
      while (true) {
        while (XPending(display)) {
          XEvent event;
          XNextEvent(display, &event);
          if (event.type == SelectionRequest) {
            serve_selection(display, owned, &event.xselectionrequest, utf8, targets);
            XFlush(display);
          } else if (event.type == SelectionNotify) {
            if (event.xselection.property != None) {
              read_property();
            }
          } else if (event.type == PropertyNotify && transfer.active() && event.xproperty.atom == property && event.xproperty.state == PropertyNewValue) {
            read_property();
          } else if (event.type == event_base + XFixesSelectionNotify) {
            auto *selection = reinterpret_cast<XFixesSelectionNotifyEvent *>(&event);
            if (selection->selection == clipboard_atom && selection->owner != window && selection->owner != None) {
              transfer = {};
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
          transfer = {};
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

  void clipboard_set(std::string_view text, const clipboard_queue_t &origin) {
    std::string capped {text};
    if (capped.size() > clipboard_max_bytes) {
      capped.resize(clipboard_max_bytes);
    }

    std::vector<clipboard_queue_t> queues;
    {
      auto &clipboard = state();
      std::lock_guard lock {clipboard.mutex};
      if (clipboard.failed || !clipboard.ready) {
        return;
      }
      clipboard.pending = capped;
      clipboard.has_pending = true;
      queues = clipboard.subscribers.note_local(origin, capped);
      if (clipboard.wake_write >= 0) {
        char byte = 1;
        if (write(clipboard.wake_write, &byte, 1) < 0) {
          BOOST_LOG(debug) << "Clipboard watcher wakeup was not queued"sv;
        }
      }
    }
    raise_clipboard(queues, capped);
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
    if (!clipboard.ready || !clipboard.subscribers.add(std::move(queue))) {
      return;
    }
    clipboard.pull = true;
    if (clipboard.wake_write >= 0) {
      char byte = 1;
      if (write(clipboard.wake_write, &byte, 1) < 0) {
        BOOST_LOG(debug) << "Clipboard watcher wakeup was not queued"sv;
      }
    }
  }

  void clipboard_unsubscribe(const clipboard_queue_t &queue) {
    auto &clipboard = state();
    std::lock_guard lock {clipboard.mutex};
    clipboard.subscribers.remove(queue);
  }
}  // namespace platf

#endif
