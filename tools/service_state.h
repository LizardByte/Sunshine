/**
 * @file tools/service_state.h
 * @brief Instance-owned Windows service controls and event lifetimes.
 */
#pragma once
#include <functional>
#include <mutex>
#include <Windows.h>
#include <WtsApi32.h>

namespace service {
  /**
   * @brief Native operations used by the service state owner.
   */
  struct operations_t {
    std::function<BOOL(SERVICE_STATUS_HANDLE, LPSERVICE_STATUS)> report {SetServiceStatus};  ///< Report state to SCM.
    std::function<BOOL(HANDLE)> signal {SetEvent};  ///< Wake service work.
    std::function<BOOL(HANDLE)> close {CloseHandle};  ///< Release owned event handles.
  };

  /**
   * @brief Own one ServiceMain invocation's status and control events.
   */
  class state_t {
  public:
    /**
     * @brief Bind native operations.
     * @param operations Native or test operations.
     */
    explicit state_t(operations_t operations = {}):
        operations(std::move(operations)) {
      status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    }

    /**
     * @brief Release successfully created events after service work stops.
     */
    ~state_t() {
      if (session_change_event) {
        operations.close(session_change_event);
      }
      if (stop_event) {
        operations.close(stop_event);
      }
    }

    /**
     * @brief Service owners cannot be copied.
     */
    state_t(const state_t &) = delete;
    /**
     * @brief Service owners cannot be assigned.
     */
    state_t &operator=(const state_t &) = delete;
    SERVICE_STATUS_HANDLE status_handle {};  ///< SCM registration for this invocation.
    HANDLE stop_event {};  ///< Manual-reset shutdown event.
    HANDLE session_change_event {};  ///< Auto-reset console-session event.

    /**
     * @brief Report a complete state transition under the control lock.
     * @param state New service state.
     * @param controls Accepted controls.
     * @param error Native startup error.
     * @param wait_hint Cleanup deadline hint.
     */
    void report(DWORD state, DWORD controls = 0, DWORD error = NO_ERROR, DWORD wait_hint = 0) {
      std::lock_guard guard(mutex);
      status.dwCurrentState = state;
      status.dwControlsAccepted = controls;
      status.dwWin32ExitCode = error;
      status.dwWaitHint = wait_hint;
      operations.report(status_handle, &status);
    }

    /**
     * @brief Dispatch a control to this service's status and events.
     * @param control SCM control.
     * @param event_type Session-change type.
     * @return Native control result.
     */
    DWORD control(DWORD control, DWORD event_type) {
      switch (control) {
        case SERVICE_CONTROL_INTERROGATE:
          return NO_ERROR;
        case SERVICE_CONTROL_SESSIONCHANGE:
          if (event_type == WTS_CONSOLE_CONNECT && session_change_event) {
            operations.signal(session_change_event);
          }
          return NO_ERROR;
        case SERVICE_CONTROL_PRESHUTDOWN:
        case SERVICE_CONTROL_STOP:
          report(SERVICE_STOP_PENDING, 0, NO_ERROR, 30000);
          if (stop_event) {
            operations.signal(stop_event);
          }
          return NO_ERROR;
        default:
          return ERROR_CALL_NOT_IMPLEMENTED;
      }
    }

  private:
    operations_t operations;  ///< Operations outliving owned events.
    std::mutex mutex;  ///< Serialize SCM and ServiceMain status transitions.
    SERVICE_STATUS status {};  ///< Last reported SCM state.
  };
}  // namespace service
