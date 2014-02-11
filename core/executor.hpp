#ifndef SPARKS_CORE_EXECUTOR_HPP_
#define SPARKS_CORE_EXECUTOR_HPP_

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

#include "blocking_counter.hpp"
#include "id_vector_fwd.hpp"

namespace sparks {

class Executor {
 public:
  using TaskId = uint32_t;
  using ThreadId = uint16_t;
  using TaskStamp = uint16_t;
  using Closure = std::function<void(void)>;

  static const ThreadId MAX_THREADS = 16;
  static const ThreadId NO_AFFINITY = static_cast<ThreadId>(-1);

 private:
  struct Task;

 public:
  static const TaskId INVALID_TASK = IdVector32<Task>::INVALID_INDEX;

  Executor();
  ~Executor();

  void add_task(Closure closure, ThreadId affinity = NO_AFFINITY);
  void run_tasks(ThreadId with_affinity = NO_AFFINITY);

  void wait_for_completion() { num_scheduled_tasks_.wait_and_disable(); }
  void stop();

 private:
  struct Task {
    template <class ClosureType>
    Task(ClosureType&& closure, TaskStamp stamp)
        : closure(std::forward<ClosureType>(closure)),
          stamp(stamp),
          next_task(INVALID_TASK) {}

    Closure closure;
    TaskStamp stamp;
    TaskId next_task;
  };

  struct TaskList {
    TaskId first = INVALID_TASK;
    TaskId last = INVALID_TASK;
  };

  inline bool run_one_no_affinity();
  inline bool run_one_with_affinity(TaskList& thread_queue);

  static inline bool empty(TaskList& queue);
  inline void add_to_queue(TaskList& queue, TaskId, Task& task);
  inline void pop_and_run(std::unique_lock<std::mutex>& lock, TaskList& queue);
  inline void pop_and_run(std::unique_lock<std::mutex>& lock, TaskList& queue,
                          Task& task);
  inline void clear_queue(TaskList& queue);

  bool stopped_ = false;
  BlockingCounter active_threads_blocking_counter_;
  bool handled_affinities[MAX_THREADS] = {false};
  uint32_t num_sleeping_threads_ = 0;
  BlockingCounter num_scheduled_tasks_;
  TaskStamp next_task_stamp_ = 0;

  IdVector32<Task> tasks_;
  TaskList main_queue_;
  TaskList per_thread_queue_[MAX_THREADS];

  std::mutex mutex_;
  std::condition_variable not_empty_condition_;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_EXECUTOR_HPP_
