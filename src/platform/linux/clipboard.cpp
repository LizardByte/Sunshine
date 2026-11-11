/**
 * @file src/platform/linux/clipboard.cpp
 * @brief X11 CLIPBOARD sync for a Sunshine session.
 */
#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef SUNSHINE_BUILD_X11

namespace platf {
  void clipboard_set(clipboard_data_t, const clipboard_queue_t &) {
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
     * @brief How long an INCR send may wait for the requestor before it is dropped.
     */
    constexpr auto incr_send_timeout = std::chrono::seconds {30};

    /**
     * @brief Process-wide X11 clipboard watcher state.
     */
    struct state_t {
      std::mutex mutex;  ///< Guards the fields below for the input and X11 threads.
      std::condition_variable ready_cv;  ///< Wakes subscribers waiting for the display connection.
      bool ready = false;  ///< Whether the X11 watcher can accept clipboard operations.
      bool failed = false;  ///< Whether opening the display or XFixes failed.
      bool started = false;  ///< Whether the watcher thread has been launched.
      bool pull = false;  ///< Whether a new subscriber still needs the current host clipboard.
      int wake_write = -1;  ///< Write end of the pipe that wakes the watcher thread.
      std::shared_ptr<const clipboard_data_t> pending;  ///< Client clipboard waiting to become the selection.
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
     * @brief Wake the watcher thread so it handles new state.
     *
     * @param clipboard Watcher state. The caller holds its lock.
     */
    void wake_watcher(const state_t &clipboard) {
      if (clipboard.wake_write >= 0) {
        char byte = 1;
        if (write(clipboard.wake_write, &byte, 1) < 0) {
          BOOST_LOG(debug) << "Clipboard watcher wakeup was not queued"sv;
        }
      }
    }

    /**
     * @brief Queue a clipboard offer for each selected subscriber.
     *
     * @param delivery Subscribers and sequence chosen by clipboard_subscribers_t::publish().
     * @param data Clipboard to offer.
     */
    void raise_offers(const clipboard_delivery_t &delivery, const std::shared_ptr<const clipboard_data_t> &data) {
      for (const auto &queue : delivery.queues) {
        queue->raise(clipboard_message_t {clipboard_action_e::offer, delivery.sequence, {}, data});
      }
    }

    /**
     * @brief Offer a host clipboard to every subscriber that does not already have it.
     *
     * @param data Host clipboard.
     */
    void publish(const std::shared_ptr<const clipboard_data_t> &data) {
      clipboard_delivery_t delivery;
      {
        auto &clipboard = state();
        std::lock_guard lock {clipboard.mutex};
        delivery = clipboard.subscribers.publish(data);
      }
      raise_offers(delivery, data);
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
     * @brief Display connection owned by the watcher thread.
     */
    std::atomic<Display *> watcher_display {nullptr};

    /**
     * @brief X error handler that was installed before the watcher's.
     */
    XErrorHandler previous_error_handler = nullptr;

    /**
     * @brief Ignore X errors on the watcher's display and pass the rest on.
     *
     * Other clients can destroy their windows during a selection transfer. The default
     * Xlib handler exits the process on the resulting BadWindow error.
     *
     * @param display Display the error happened on.
     * @param event X error.
     * @return Zero. Xlib ignores the return value.
     */
    int handle_x_error(Display *display, XErrorEvent *event) {
      if (display == watcher_display.load()) {
        BOOST_LOG(debug) << "Clipboard sync ignored X error "sv << static_cast<int>(event->error_code);
        return 0;
      }
      return previous_error_handler != nullptr ? previous_error_handler(display, event) : 0;
    }

    /**
     * @brief Read one slice of the selection property.
     *
     * Format-32 items arrive from Xlib as longs. They are stored as 4-byte values so the
     * slice uses the same 32-bit units as the X property offset.
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
        if (actual_format == 32) {
          fragment.data.resize(item_count * 4);
          for (unsigned long i = 0; i < item_count; ++i) {
            const auto item = static_cast<std::uint32_t>(reinterpret_cast<const long *>(data)[i]);
            std::memcpy(fragment.data.data() + i * 4, &item, sizeof(item));
          }
        } else {
          const auto nbytes = std::min(item_count * property_byte_width(actual_format), static_cast<std::size_t>(length) * 4);
          fragment.data.resize(nbytes);
          std::copy_n(reinterpret_cast<const std::byte *>(data), nbytes, fragment.data.begin());
        }
        XFree(data);
      }
      return fragment;
    }

    /**
     * @brief Watches the X11 CLIPBOARD selection, reads host copies, and serves client copies.
     *
     * Every method runs on the watcher thread.
     */
    class watcher_t {
    public:
      /**
       * @brief Set up the atoms and transfer limits for one display connection.
       *
       * @param display X display connection owned by the watcher thread.
       * @param window Window used to own and request the selection.
       * @param event_base First XFixes event code.
       */
      watcher_t(Display *display, Window window, int event_base):
          display_ {display},
          window_ {window},
          event_base_ {event_base},
          clipboard_ {XInternAtom(display, "CLIPBOARD", False)},
          targets_ {XInternAtom(display, "TARGETS", False)},
          utf8_ {XInternAtom(display, "UTF8_STRING", False)},
          incr_ {XInternAtom(display, "INCR", False)},
          png_ {XInternAtom(display, SS_CLIPBOARD_MIME_PNG, False)},
          jpeg_ {XInternAtom(display, SS_CLIPBOARD_MIME_JPEG, False)},
          property_ {XInternAtom(display, "SUNSHINE_CLIPBOARD", False)},
          // Keep each property write well under the server's request size limit
          chunk_size_ {std::min<std::size_t>(256 * 1024, static_cast<std::size_t>(XMaxRequestSize(display)) * 4 - 64)} {
        XFixesSelectSelectionInput(display_, window_, clipboard_, XFixesSetSelectionOwnerNotifyMask);
        XFlush(display_);
      }

      /**
       * @brief Handle one X event.
       *
       * @param event Event just removed from the queue.
       */
      void handle(const XEvent &event) {
        if (event.type == SelectionRequest) {
          serve(event.xselectionrequest);
        } else if (event.type == SelectionNotify) {
          converted(event.xselection);
        } else if (event.type == PropertyNotify && event.xproperty.window == window_) {
          if (event.xproperty.atom == property_ && event.xproperty.state == PropertyNewValue && fetching_ && transfer_.active()) {
            read_reply();
          }
        } else if (event.type == PropertyNotify) {
          continue_send(event.xproperty);
        } else if (event.type == event_base_ + XFixesSelectionNotify) {
          XFixesSelectionNotifyEvent selection {};
          std::memcpy(&selection, &event, sizeof(selection));
          if (selection.selection == clipboard_ && selection.owner != window_ && selection.owner != None) {
            owned_.reset();
            start_fetch();
          }
        }
      }

      /**
       * @brief Take ownership of the selection when a client clipboard is waiting.
       */
      void take_pending() {
        std::shared_ptr<const clipboard_data_t> data;
        {
          auto &clipboard = state();
          std::lock_guard lock {clipboard.mutex};
          data = std::move(clipboard.pending);
        }
        if (!data) {
          return;
        }

        // A host clipboard still being read is now stale
        fetching_ = false;
        owned_ = std::move(data);
        XSetSelectionOwner(display_, clipboard_, window_, CurrentTime);
        XFlush(display_);
      }

      /**
       * @brief Offer the current host clipboard to a new subscriber.
       */
      void pull() {
        if (fetching_) {
          // The read in progress will reach the new subscriber too
          return;
        }
        if (owned_ && XGetSelectionOwner(display_, clipboard_) == window_) {
          publish(owned_);
        } else {
          start_fetch();
        }
      }

      /**
       * @brief Drop INCR sends whose requestor stopped reading.
       */
      void prune_sends() {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(sends_, [this, now](const send_t &send) {
          if (now - send.started < incr_send_timeout) {
            return false;
          }
          XSelectInput(display_, send.requestor, NoEventMask);
          return true;
        });
      }

    private:
      /**
       * @brief Value Sunshine sends to one requestor with the INCR protocol.
       */
      struct send_t {
        Window requestor;  ///< Window that requested the value.
        Atom property;  ///< Property the requestor reads.
        Atom type;  ///< Type of the value.
        std::span<const std::uint8_t> bytes;  ///< Value being sent. data keeps it alive.
        std::shared_ptr<const clipboard_data_t> data;  ///< Clipboard that holds bytes.
        incr_sender_t sender;  ///< Slices still to write.
        std::chrono::steady_clock::time_point started;  ///< When the transfer began.
      };

      /**
       * @brief Ask the selection owner to convert the clipboard to one target.
       *
       * @param target Target atom.
       */
      void request(Atom target) {
        current_ = target;
        transfer_ = {};
        XDeleteProperty(display_, window_, property_);
        XConvertSelection(display_, clipboard_, target, property_, window_, CurrentTime);
        XFlush(display_);
      }

      /**
       * @brief Start reading the host clipboard, beginning with its target list.
       */
      void start_fetch() {
        fetching_ = true;
        fetched_ = {};
        remaining_.clear();
        request(targets_);
      }

      /**
       * @brief Request the next wanted target, or publish what was read.
       */
      void next_target() {
        if (remaining_.empty()) {
          fetching_ = false;
          if (!fetched_.text.empty() || !fetched_.image.empty()) {
            publish(std::make_shared<const clipboard_data_t>(std::move(fetched_)));
          }
          fetched_ = {};
          return;
        }
        const auto target = remaining_.front();
        remaining_.erase(remaining_.begin());
        request(target);
      }

      /**
       * @brief Handle the owner's answer to a conversion request.
       *
       * @param event SelectionNotify event.
       */
      void converted(const XSelectionEvent &event) {
        if (!fetching_ || event.requestor != window_ || event.target != current_) {
          return;
        }
        if (event.property == None) {
          // Owners that predate TARGETS can still convert to UTF8_STRING
          if (current_ == targets_) {
            remaining_ = {utf8_};
          }
          next_target();
          return;
        }
        read_reply();
      }

      /**
       * @brief Read the reply property for the current target.
       */
      void read_reply() {
        const auto loaded = load_selection_property([this](unsigned long offset, unsigned long length) {
          return read_selection_fragment(display_, window_, property_, offset, length);
        });
        if (!loaded) {
          next_target();
          return;
        }
        if (!loaded->acknowledged) {
          XDeleteProperty(display_, window_, property_);
        }

        if (current_ == targets_) {
          choose_targets(*loaded);
          next_target();
          return;
        }

        auto value = transfer_.consume(loaded->type, loaded->format, loaded->data, loaded->truncated, current_, incr_);
        if (value) {
          store(std::move(*value));
        } else if (transfer_.active()) {
          // Wait for the next INCR chunk
          return;
        }
        next_target();
      }

      /**
       * @brief Pick the text and image targets to read from the owner's target list.
       *
       * @param loaded TARGETS reply.
       */
      void choose_targets(const loaded_selection_t &loaded) {
        std::vector<Atom> offered;
        if (loaded.type == XA_ATOM && loaded.format == 32) {
          for (std::size_t i = 0; i + 4 <= loaded.data.size(); i += 4) {
            std::uint32_t atom = 0;
            std::memcpy(&atom, loaded.data.data() + i, sizeof(atom));
            offered.push_back(atom);
          }
        }
        const auto has = [&offered](Atom atom) {
          return std::ranges::find(offered, atom) != offered.end();
        };

        remaining_.clear();
        if (has(utf8_)) {
          remaining_.push_back(utf8_);
        } else if (has(XA_STRING)) {
          remaining_.push_back(XA_STRING);
        }
        if (has(png_)) {
          remaining_.push_back(png_);
        } else if (has(jpeg_)) {
          remaining_.push_back(jpeg_);
        }
      }

      /**
       * @brief Keep a value read for the current target.
       *
       * @param value Bytes of the converted selection.
       */
      void store(std::vector<std::byte> value) {
        const std::string_view bytes {reinterpret_cast<const char *>(value.data()), value.size()};
        if (current_ == utf8_) {
          fetched_.text = bytes;
        } else if (current_ == XA_STRING) {
          fetched_.text = latin1_to_utf8(bytes);
        } else if (!value.empty()) {
          fetched_.image_mime = current_ == png_ ? SS_CLIPBOARD_MIME_PNG : SS_CLIPBOARD_MIME_JPEG;
          fetched_.image.assign(bytes.begin(), bytes.end());
        }
      }

      /**
       * @brief Answer one selection request with the clipboard Sunshine owns.
       *
       * @param request Selection request from another client.
       */
      void serve(const XSelectionRequestEvent &request) {
        const auto data = owned_;
        std::optional<std::span<const std::uint8_t>> bytes;
        if (data && (request.target == utf8_ || request.target == XA_STRING)) {
          bytes = clipboard_payload(*data, SS_CLIPBOARD_MIME_TEXT);
        } else if (data && (request.target == png_ || request.target == jpeg_)) {
          bytes = clipboard_payload(*data, request.target == png_ ? SS_CLIPBOARD_MIME_PNG : SS_CLIPBOARD_MIME_JPEG);
        }

        const bool supported = request.target == targets_ || bytes.has_value();
        const auto property = static_cast<Atom>(selection_reply_property(request.property, request.target, supported));
        if (property != None && request.target == targets_) {
          std::vector<Atom> list {targets_};
          if (data && !data->text.empty()) {
            list.push_back(utf8_);
            list.push_back(XA_STRING);
          }
          if (data && !data->image_mime.empty()) {
            list.push_back(data->image_mime == SS_CLIPBOARD_MIME_PNG ? png_ : jpeg_);
          }
          XChangeProperty(display_, request.requestor, property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char *>(list.data()), static_cast<int>(list.size()));
        } else if (property != None && bytes->size() <= chunk_size_) {
          XChangeProperty(display_, request.requestor, property, request.target, 8, PropModeReplace, bytes->data(), static_cast<int>(bytes->size()));
        } else if (property != None) {
          // Too large for one request, so announce the size and send it in INCR slices
          XSelectInput(display_, request.requestor, PropertyChangeMask);
          const long size = static_cast<long>(bytes->size());
          XChangeProperty(display_, request.requestor, property, incr_, 32, PropModeReplace, reinterpret_cast<const unsigned char *>(&size), 1);
          sends_.push_back(send_t {request.requestor, property, request.target, *bytes, data, incr_sender_t {bytes->size(), chunk_size_}, std::chrono::steady_clock::now()});
        }

        XEvent notify {};
        notify.xselection.type = SelectionNotify;
        notify.xselection.display = request.display;
        notify.xselection.requestor = request.requestor;
        notify.xselection.selection = request.selection;
        notify.xselection.target = request.target;
        notify.xselection.property = property;
        notify.xselection.time = request.time;
        XSendEvent(display_, request.requestor, False, 0, &notify);
        XFlush(display_);
      }

      /**
       * @brief Write the next INCR slice after the requestor read the last one.
       *
       * @param event PropertyNotify event from a requestor window.
       */
      void continue_send(const XPropertyEvent &event) {
        if (event.state != PropertyDelete) {
          return;
        }
        const auto send = std::ranges::find_if(sends_, [&event](const send_t &candidate) {
          return candidate.requestor == event.window && candidate.property == event.atom;
        });
        if (send == sends_.end()) {
          return;
        }

        const auto [offset, length] = send->sender.next();
        XChangeProperty(display_, send->requestor, send->property, send->type, 8, PropModeReplace, send->bytes.data() + offset, static_cast<int>(length));
        if (send->sender.done()) {
          XSelectInput(display_, send->requestor, NoEventMask);
          sends_.erase(send);
        }
        XFlush(display_);
      }

      Display *display_;  ///< X display connection owned by the watcher thread.
      Window window_;  ///< Window used to own and request the selection.
      int event_base_;  ///< First XFixes event code.
      Atom clipboard_;  ///< CLIPBOARD atom.
      Atom targets_;  ///< TARGETS atom.
      Atom utf8_;  ///< UTF8_STRING atom.
      Atom incr_;  ///< INCR atom.
      Atom png_;  ///< image/png atom.
      Atom jpeg_;  ///< image/jpeg atom.
      Atom property_;  ///< Property that receives conversion replies.
      std::size_t chunk_size_;  ///< Largest value written with one XChangeProperty.

      std::shared_ptr<const clipboard_data_t> owned_;  ///< Clipboard Sunshine serves while it owns the selection.
      std::vector<send_t> sends_;  ///< INCR sends waiting for their requestors.

      bool fetching_ = false;  ///< Whether the host clipboard is being read.
      Atom current_ = None;  ///< Target being converted.
      std::vector<Atom> remaining_;  ///< Targets still to convert after current_.
      clipboard_data_t fetched_;  ///< Formats read so far.
      incr_transfer_t transfer_;  ///< INCR transfer for current_.
    };

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

      watcher_display = display;
      previous_error_handler = XSetErrorHandler(handle_x_error);

      auto window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
      XSelectInput(display, window, PropertyChangeMask);
      watcher_t watcher {display, window, event_base};
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
          watcher.handle(event);
        }

        watcher.take_pending();
        if (consume_pull()) {
          watcher.pull();
        }
        watcher.prune_sends();

        std::array<pollfd, 2> fds {pollfd {xfd, POLLIN, 0}, pollfd {wake_read, POLLIN, 0}};
        poll(fds.data(), fds.size(), 200);
        if ((fds[1].revents & POLLIN) != 0) {
          drain_wake(wake_read);
        }
      }
    }
  }  // namespace

  void clipboard_set(clipboard_data_t data, const clipboard_queue_t &origin) {
    if (data.text.empty() && data.image.empty()) {
      return;
    }

    const auto shared = std::make_shared<const clipboard_data_t>(std::move(data));
    clipboard_delivery_t delivery;
    {
      auto &clipboard = state();
      std::lock_guard lock {clipboard.mutex};
      if (clipboard.failed || !clipboard.ready) {
        return;
      }
      clipboard.pending = shared;
      delivery = clipboard.subscribers.publish(shared, origin);
      wake_watcher(clipboard);
    }
    raise_offers(delivery, shared);
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
    wake_watcher(clipboard);
  }

  void clipboard_unsubscribe(const clipboard_queue_t &queue) {
    auto &clipboard = state();
    std::lock_guard lock {clipboard.mutex};
    clipboard.subscribers.remove(queue);
  }
}  // namespace platf

#endif
