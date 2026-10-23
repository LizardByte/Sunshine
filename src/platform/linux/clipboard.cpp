/**
 * @file src/platform/linux/clipboard.cpp
 * @brief X11 CLIPBOARD sync for a Sunshine session.
 */
#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
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
      std::jthread thread;  ///< X11 watcher thread.
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

      for (const auto &queue : queues) {
        queue->raise(clipboard_text_t {text});
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
     * @brief Mark the watcher failed and release the display connection.
     *
     * @param wake_read Read end of the wakeup pipe, or -1 when it was not opened.
     * @param wake_write Write end of the wakeup pipe, or -1 when it was not opened.
     * @param display X display connection, or null when it was not opened.
     */
    void fail_watcher(int wake_read, int wake_write, Display *display) {
      if (display != nullptr) {
        XCloseDisplay(display);
      }
      if (wake_read >= 0) {
        close(wake_read);
      }
      if (wake_write >= 0) {
        close(wake_write);
      }
      auto &clipboard = state();
      std::lock_guard lock {clipboard.mutex};
      clipboard.failed = true;
      clipboard.ready_cv.notify_all();
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
    void serve_selection(Display *display, std::string_view text, const XSelectionRequestEvent *request, Atom utf8, Atom targets) {
      XSelectionEvent selection {};
      selection.type = SelectionNotify;
      selection.display = request->display;
      selection.requestor = request->requestor;
      selection.selection = request->selection;
      selection.target = request->target;
      selection.time = request->time;

      const bool supported = request->target == targets || request->target == utf8 || request->target == XA_STRING;
      const auto property = static_cast<Atom>(selection_reply_property(request->property, request->target, supported));
      selection.property = property;
      if (property != None && request->target == targets) {
        const std::array<Atom, 3> list {utf8, XA_STRING, targets};
        std::array<std::byte, sizeof(list)> bytes {};
        std::memcpy(bytes.data(), list.data(), bytes.size());
        XChangeProperty(display, request->requestor, property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char *>(bytes.data()), static_cast<int>(list.size()));
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

      XEvent notify {};
      notify.xselection = selection;
      XSendEvent(display, request->requestor, False, 0, &notify);
    }

    /**
     * @brief Read one slice of the selection property.
     *
     * @param display X display connection owned by the watcher thread.
     * @param window Window that holds the property.
     * @param property Property atom being read.
     * @param offset Offset in 32-bit units.
     * @param length Maximum length in 32-bit units.
     * @return The slice. ok is false when the X call fails.
     */
    selection_fragment_t read_selection_fragment(Display *display, Window window, Atom property, unsigned long offset, unsigned long length) {
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
    }

    /**
     * @brief Publish clipboard text from the window property, deleting an unread tail.
     *
     * @param display X display connection owned by the watcher thread.
     * @param window Window that holds the property.
     * @param property Property atom being read.
     * @param utf8 UTF8_STRING atom.
     * @param incr INCR atom.
     * @param transfer Incremental transfer assembled across property events.
     */
    void publish_property(Display *display, Window window, Atom property, Atom utf8, Atom incr, incr_transfer_t &transfer) {
      const auto loaded = load_selection_property([display, window, property](unsigned long offset, unsigned long length) {
        return read_selection_fragment(display, window, property, offset, length);
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
      if (const auto text = transfer.consume(loaded->type, loaded->format, items, bytes, utf8, XA_STRING, incr)) {
        publish(*text);
      }
    }

    /**
     * @brief Take ownership of the selection when a client clipboard is waiting.
     *
     * @param display X display connection owned by the watcher thread.
     * @param clipboard_atom CLIPBOARD atom.
     * @param window Window that becomes the selection owner.
     * @param owned Text Sunshine now owns.
     */
    void apply_pending(Display *display, Atom clipboard_atom, Window window, std::string &owned) {
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
      owned = std::move(text);
      XSetSelectionOwner(display, clipboard_atom, window, CurrentTime);
      XFlush(display);
    }

    /**
     * @brief Ask the selection owner for its UTF-8 text.
     *
     * @param display X display connection owned by the watcher thread.
     * @param clipboard_atom CLIPBOARD atom.
     * @param utf8 UTF8_STRING atom.
     * @param property Property that receives the reply.
     * @param window Requestor window.
     */
    void convert_selection(Display *display, Atom clipboard_atom, Atom utf8, Atom property, Window window) {
      XConvertSelection(display, clipboard_atom, utf8, property, window, CurrentTime);
      XFlush(display);
    }

    /**
     * @brief Handle one X event for the clipboard watcher.
     *
     * @param display X display connection owned by the watcher thread.
     * @param event Event just removed from the queue.
     * @param event_base First XFixes event code.
     * @param window Watcher window.
     * @param clipboard_atom CLIPBOARD atom.
     * @param utf8 UTF8_STRING atom.
     * @param incr INCR atom.
     * @param targets TARGETS atom.
     * @param property Property used for selection replies.
     * @param owned Text Sunshine owns, when it owns the selection.
     * @param transfer Incremental transfer assembled across property events.
     */
    void handle_event(Display *display, const XEvent &event, int event_base, Window window, Atom clipboard_atom, Atom utf8, Atom incr, Atom targets, Atom property, std::string &owned, incr_transfer_t &transfer) {
      if (event.type == SelectionRequest) {
        serve_selection(display, owned, &event.xselectionrequest, utf8, targets);
        XFlush(display);
      } else if (event.type == SelectionNotify) {
        if (event.xselection.property != None) {
          publish_property(display, window, property, utf8, incr, transfer);
        }
      } else if (event.type == PropertyNotify && transfer.active() && event.xproperty.atom == property && event.xproperty.state == PropertyNewValue) {
        publish_property(display, window, property, utf8, incr, transfer);
      } else if (event.type == event_base + XFixesSelectionNotify) {
        XFixesSelectionNotifyEvent selection {};
        std::memcpy(&selection, &event, sizeof(selection));
        if (selection.selection == clipboard_atom && selection.owner != window && selection.owner != None) {
          transfer = {};
          convert_selection(display, clipboard_atom, utf8, property, window);
        }
      }
    }

    /**
     * @brief Take and clear the request for the current host clipboard.
     *
     * @return True when a subscriber is still waiting for the current selection.
     */
    bool consume_pull() {
      auto &clipboard = state();
      std::lock_guard lock {clipboard.mutex};
      const bool pull = clipboard.pull;
      clipboard.pull = false;
      return pull;
    }

    /**
     * @brief Discard wakeup bytes that are only used to leave poll.
     *
     * @param wake_read Read end of the wakeup pipe.
     */
    void drain_wake(int wake_read) {
      std::array<std::byte, 64> buffer {};
      while (read(wake_read, buffer.data(), buffer.size()) > 0) {
        // The pipe only wakes the watcher. The bytes are not a message.
      }
    }

    /**
     * @brief Watch the X11 CLIPBOARD selection until the process exits.
     *
     * @param stop Stop request delivered when the watcher thread is destroyed.
     */
    void thread_main(std::stop_token stop) {
      std::array<int, 2> pipes {-1, -1};
      if (pipe2(pipes.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        fail_watcher(-1, -1, nullptr);
        return;
      }
      const int wake_read = pipes[0];
      const int wake_write = pipes[1];

      Display *display = XOpenDisplay(nullptr);
      if (display == nullptr) {
        BOOST_LOG(warning) << "Clipboard sync could not open the X display"sv;
        fail_watcher(wake_read, wake_write, nullptr);
        return;
      }

      int event_base = 0;
      int error_base = 0;
      if (!XFixesQueryExtension(display, &event_base, &error_base)) {
        BOOST_LOG(warning) << "Clipboard sync requires the XFixes extension"sv;
        fail_watcher(wake_read, wake_write, display);
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

      const int xfd = ConnectionNumber(display);
      while (!stop.stop_requested()) {
        while (XPending(display) != 0) {
          XEvent event;
          XNextEvent(display, &event);
          handle_event(display, event, event_base, window, clipboard_atom, utf8, incr, targets, property, owned, transfer);
        }

        apply_pending(display, clipboard_atom, window, owned);
        if (consume_pull()) {
          transfer = {};
          convert_selection(display, clipboard_atom, utf8, property, window);
        }

        std::array<pollfd, 2> fds {pollfd {xfd, POLLIN, 0}, pollfd {wake_read, POLLIN, 0}};
        poll(fds.data(), fds.size(), 200);
        if ((fds[1].revents & POLLIN) != 0) {
          drain_wake(wake_read);
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
      clipboard.thread = std::jthread(thread_main);
    }
    clipboard.ready_cv.wait(lock, [&clipboard]() {
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
