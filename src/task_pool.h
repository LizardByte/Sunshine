/**
 * @file src/task_pool.h
 * @brief Declarations for the task pool system.
 */
#pragma once

// standard includes
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// local includes
#include "move_by_copy.h"
#include "utility.h"

namespace task_pool_util {

  /**
   * @brief Type-erased task interface stored by the task pool.
   */
  class _ImplBase {
  public:
    // _unique_base_type _this_ptr;

    inline virtual ~_ImplBase() = default;

    /**
     * @brief Execute the queued task body.
     */
    virtual void run() = 0;
  };

  /**
   * @brief Concrete task wrapper that stores and invokes a callable.
   */
  template<class Function>
  class _Impl: public _ImplBase {
    Function _func;

  public:
    /**
     * @brief Store a concrete callable behind the task type-erasure interface.
     *
     * @param f Callable executed by the helper.
     */
    _Impl(Function &&f):
        _func(std::forward<Function>(f)) {
    }

    /**
     * @brief Execute the queued task body.
     */
    void run() override {
      _func();
    }
  };

  /**
   * @brief Queue of immediate and delayed tasks executed by worker threads.
   */
  class TaskPool {
  public:
    /**
     * @brief Type-erased task owned by the task pool.
     */
    typedef std::unique_ptr<_ImplBase> __task;
    /**
     * @brief Stable pointer used to identify a queued task.
     */
    typedef _ImplBase *task_id_t;

    /**
     * @brief Steady-clock timestamp used to schedule delayed tasks.
     */
    typedef std::chrono::steady_clock::time_point __time_point;

    /**
     * @brief Queued delayed task identifier paired with its future result.
     */
    template<class R>
    class timer_task_t {
    public:
      task_id_t task_id;  ///< Task ID.
      std::future<R> future;  ///< Future that resolves with the timer task result.

      /**
       * @brief Track a queued timer task and the future for its result.
       *
       * @param task_id Identifier returned when the task was queued.
       * @param future Future object whose result will be collected later.
       */
      timer_task_t(task_id_t task_id, std::future<R> &future):
          task_id {task_id},
          future {std::move(future)} {
      }
    };

  protected:
    std::deque<__task> _tasks;  ///< Immediate tasks waiting for worker execution.
    std::vector<std::pair<__time_point, __task>> _timer_tasks;  ///< Delayed tasks sorted by their due time.
    std::mutex _task_mutex;  ///< Mutex protecting task queues and worker wakeups.

  public:
    TaskPool() = default;

    /**
     * @brief Move queued tasks and timers from another task pool.
     *
     * @param other Pool whose queues and worker state are moved into this pool.
     */
    TaskPool(TaskPool &&other) noexcept:
        _tasks {std::move(other._tasks)},
        _timer_tasks {std::move(other._timer_tasks)} {
    }

    /**
     * @brief Assign state from another instance while preserving ownership semantics.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference or value produced by the operator.
     */
    TaskPool &operator=(TaskPool &&other) noexcept {
      std::swap(_tasks, other._tasks);
      std::swap(_timer_tasks, other._timer_tasks);

      return *this;
    }

    /**
     * @brief Queue work for asynchronous execution.
     *
     * @param newTask New task.
     * @param args Arguments forwarded to the callable or parser.
     * @return Identifier assigned to the queued task.
     */
    template<class Function, class... Args>
    auto push(Function &&newTask, Args &&...args) {
      static_assert(std::is_invocable_v<Function, Args &&...>, "arguments don't match the function");

      using __return = std::invoke_result_t<Function, Args &&...>;
      using task_t = std::packaged_task<__return()>;

      auto bind = [task = std::forward<Function>(newTask), tuple_args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        return std::apply(task, std::move(tuple_args));
      };

      task_t task(std::move(bind));

      auto future = task.get_future();

      std::lock_guard<std::mutex> lg(_task_mutex);
      _tasks.emplace_back(toRunnable(std::move(task)));

      return future;
    }

    /**
     * @brief Queue a task that becomes runnable after a delay.
     *
     * @param task Task object to enqueue or execute.
     */
    void pushDelayed(std::pair<__time_point, __task> &&task) {
      std::lock_guard lg(_task_mutex);

      auto it = _timer_tasks.cbegin();
      for (; it < _timer_tasks.cend(); ++it) {
        if (std::get<0>(*it) < task.first) {
          break;
        }
      }

      _timer_tasks.emplace(it, task.first, std::move(task.second));
    }

    /**
     * @return An id to potentially delay the task.
     *
     * @param newTask New task.
     * @param duration Delay before the timed task should run.
     * @param args Arguments forwarded to the callable or parser.
     */
    template<class Function, class X, class Y, class... Args>
    auto pushDelayed(Function &&newTask, std::chrono::duration<X, Y> duration, Args &&...args) {
      static_assert(std::is_invocable_v<Function, Args &&...>, "arguments don't match the function");

      using __return = std::invoke_result_t<Function, Args &&...>;
      using task_t = std::packaged_task<__return()>;

      __time_point time_point;
      if constexpr (std::is_floating_point_v<X>) {
        time_point = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
      } else {
        time_point = std::chrono::steady_clock::now() + duration;
      }

      auto bind = [task = std::forward<Function>(newTask), tuple_args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        return std::apply(task, std::move(tuple_args));
      };

      task_t task(std::move(bind));

      auto future = task.get_future();
      auto runnable = toRunnable(std::move(task));

      task_id_t task_id = &*runnable;

      pushDelayed(std::pair {time_point, std::move(runnable)});

      return timer_task_t<__return> {task_id, future};
    }

    /**
     * @param task_id The id of the task to delay.
     * @param duration The delay before executing the task.
     */
    template<class X, class Y>
    void delay(task_id_t task_id, std::chrono::duration<X, Y> duration) {
      std::lock_guard<std::mutex> lg(_task_mutex);

      auto it = _timer_tasks.begin();
      for (; it < _timer_tasks.cend(); ++it) {
        const __task &task = std::get<1>(*it);

        if (&*task == task_id) {
          std::get<0>(*it) = std::chrono::steady_clock::now() + duration;

          break;
        }
      }

      if (it == _timer_tasks.cend()) {
        return;
      }

      // smaller time goes to the back
      auto prev = it - 1;
      while (it > _timer_tasks.cbegin()) {
        if (std::get<0>(*it) > std::get<0>(*prev)) {
          std::swap(*it, *prev);
        }

        --prev;
        --it;
      }
    }

    /**
     * @brief Cancel a queued or delayed task before it runs.
     *
     * @param task_id Identifier returned when the task was queued.
     * @return True when the task was found and cancelled.
     */
    bool cancel(task_id_t task_id) {
      std::lock_guard lg(_task_mutex);

      auto it = _timer_tasks.begin();
      for (; it < _timer_tasks.cend(); ++it) {
        const __task &task = std::get<1>(*it);

        if (&*task == task_id) {
          _timer_tasks.erase(it);

          return true;
        }
      }

      return false;
    }

    /**
     * @brief Remove and return the next queued item, waiting when requested.
     *
     * @param task_id Identifier returned when the task was queued.
     * @return Removed queue item, or empty result when the queue is stopped or empty.
     */
    std::optional<std::pair<__time_point, __task>> pop(task_id_t task_id) {
      std::lock_guard lg(_task_mutex);

      auto pos = std::find_if(std::begin(_timer_tasks), std::end(_timer_tasks), [&task_id](const auto &t) {
        return t.second.get() == task_id;
      });

      if (pos == std::end(_timer_tasks)) {
        return std::nullopt;
      }

      return std::move(*pos);
    }

    /**
     * @brief Remove and return the next queued item, waiting when requested.
     *
     * @return Removed queue item, or empty result when the queue is stopped or empty.
     */
    std::optional<__task> pop() {
      std::lock_guard lg(_task_mutex);

      if (!_tasks.empty()) {
        __task task = std::move(_tasks.front());
        _tasks.pop_front();
        return task;
      }

      if (!_timer_tasks.empty() && std::get<0>(_timer_tasks.back()) <= std::chrono::steady_clock::now()) {
        __task task = std::move(std::get<1>(_timer_tasks.back()));
        _timer_tasks.pop_back();
        return task;
      }

      return std::nullopt;
    }

    /**
     * @brief Check whether the task pool has active workers.
     *
     * @return True when the task was found and cancelled.
     */
    bool ready() {
      std::lock_guard<std::mutex> lg(_task_mutex);

      return !_tasks.empty() || (!_timer_tasks.empty() && std::get<0>(_timer_tasks.back()) <= std::chrono::steady_clock::now());
    }

    /**
     * @brief Return the next timer task ready for execution.
     *
     * @return Time point for the next queued timer task, or std::nullopt when none exist.
     */
    std::optional<__time_point> next() {
      std::lock_guard<std::mutex> lg(_task_mutex);

      if (_timer_tasks.empty()) {
        return std::nullopt;
      }

      return std::get<0>(_timer_tasks.back());
    }

  private:
    template<class Function>
    std::unique_ptr<_ImplBase> toRunnable(Function &&f) {
      return std::make_unique<_Impl<Function>>(std::forward<Function &&>(f));
    }
  };
}  // namespace task_pool_util
