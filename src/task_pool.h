/**
 * @file src/task_pool.h
 * @brief Declarations for the task pool system.
 */
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "move_by_copy.h"
#include "utility.h"
namespace task_pool_util {

  class _ImplBase {
  public:
    // _unique_base_type _this_ptr;

    inline virtual ~_ImplBase() = default;

    virtual void
    run() = 0;
  };

  template <class Function>
  class _Impl: public _ImplBase {
    Function _func;

  public:
    _Impl(Function &&f):
        _func(std::forward<Function>(f)) {}

    void
    run() override {
      _func();
    }
  };

  class TaskPool {
  public:
    typedef std::unique_ptr<_ImplBase> __task;
    typedef _ImplBase *task_id_t;

    typedef std::chrono::steady_clock::time_point __time_point;

    template <class R>
    class timer_task_t {
    public:
      task_id_t task_id;
      std::future<R> future;

      timer_task_t(task_id_t task_id, std::future<R> &future):
          task_id { task_id }, future { std::move(future) } {}
    };

  protected:
    std::deque<__task> _tasks;
    std::vector<std::pair<__time_point, __task>> _timer_tasks;
    std::mutex _task_mutex;

  public:
    TaskPool() = default;
    TaskPool(TaskPool &&other) noexcept:
        _tasks { std::move(other._tasks) }, _timer_tasks { std::move(other._timer_tasks) } {}

    TaskPool &
    operator=(TaskPool &&other) noexcept {
      std::swap(_tasks, other._tasks);
      std::swap(_timer_tasks, other._timer_tasks);

      return *this;
    }

    template <class Function, class... Args>
    auto
    push(Function &&newTask, Args &&...args) {
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

    void
    pushDelayed(std::pair<__time_point, __task> &&task) {
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
     */
    template <class Function, class X, class Y, class... Args>
    auto
    pushDelayed(Function &&newTask, std::chrono::duration<X, Y> duration, Args &&...args) {
      static_assert(std::is_invocable_v<Function, Args &&...>, "arguments don't match the function");

      using __return = std::invoke_result_t<Function, Args &&...>;
      using task_t = std::packaged_task<__return()>;

      __time_point time_point;
      if constexpr (std::is_floating_point_v<X>) {
        time_point = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
      }
      else {
        time_point = std::chrono::steady_clock::now() + duration;
      }

      auto bind = [task = std::forward<Function>(newTask), tuple_args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        return std::apply(task, std::move(tuple_args));
      };

      task_t task(std::move(bind));

      auto future = task.get_future();
      auto runnable = toRunnable(std::move(task));

      task_id_t task_id = &*runnable;

      pushDelayed(std::pair { time_point, std::move(runnable) });

      return timer_task_t<__return> { task_id, future };
    }

    /**
     * @param task_id The id of the task to delay.
     * @param duration The delay before executing the task.
     */
    template <class X, class Y>
    void
    delay(task_id_t task_id, std::chrono::duration<X, Y> duration) {
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

    bool
    cancel(task_id_t task_id) {
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

    std::optional<std::pair<__time_point, __task>>
    pop(task_id_t task_id) {
      std::lock_guard lg(_task_mutex);

      auto pos = std::find_if(std::begin(_timer_tasks), std::end(_timer_tasks), [&task_id](const auto &t) { return t.second.get() == task_id; });

      if (pos == std::end(_timer_tasks)) {
        return std::nullopt;
      }

      return std::move(*pos);
    }

    std::optional<__task>
    pop() {
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

    bool
    ready() {
      std::lock_guard<std::mutex> lg(_task_mutex);

      return !_tasks.empty() || (!_timer_tasks.empty() && std::get<0>(_timer_tasks.back()) <= std::chrono::steady_clock::now());
    }

    std::optional<__time_point>
    next() {
      std::lock_guard<std::mutex> lg(_task_mutex);

      if (_timer_tasks.empty()) {
        return std::nullopt;
      }

      return std::get<0>(_timer_tasks.back());
    }

  private:
    template <class Function>
    std::unique_ptr<_ImplBase>
    toRunnable(Function &&f) {
      return std::make_unique<_Impl<Function>>(std::forward<Function &&>(f));
    }
  };
}  // namespace task_pool_util
