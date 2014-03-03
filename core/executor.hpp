#ifndef SPARKS_CORE_EXECUTOR_HPP_
#define SPARKS_CORE_EXECUTOR_HPP_

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <mutex>

#include "arraydelegate.hpp"
#include "blocking_counter.hpp"
#include "spin_lock.hpp"
#include "stable_id_vector.hpp"

namespace sparks {

class Executor {
 public:
  using TaskId = uint32_t;
  using DependentId = uint32_t;
  using ThreadId = uint16_t;
  using TaskStamp = uint16_t;
  using DependencyCount = uint16_t;
  using Closure = arraydelegate<void(void)>;
  using Mutex = SpinLock;

 private:
  struct Task;
  struct Dependent;

  using TaskIdVector = BasicStableIdVector<Task, TaskId, 12>;
  using DependentIdVector = BasicStableIdVector<Dependent, DependentId, 14>;

 public:
  static const ThreadId MAX_THREADS = 16;
  static const ThreadId NO_AFFINITY = static_cast<ThreadId>(-1);
  static const TaskId INVALID_TASK = TaskIdVector::INVALID_INDEX;
  static const TaskId INVALID_DEPENDENT = DependentIdVector::INVALID_INDEX;

  Executor();
  ~Executor();

  template <typename ClosureType,
            typename TaskIdRange = ::std::initializer_list<TaskId>>
  TaskId add_task(ClosureType&& closure, const TaskIdRange& depends_on = {},
                  ThreadId affinity = NO_AFFINITY);

  template <typename ClosureType, typename TaskIdFwdIter>
  TaskId add_task(ClosureType&& closure, TaskIdFwdIter depends_begin,
                  TaskIdFwdIter depends_end, ThreadId affinity = NO_AFFINITY);

  void run_tasks_no_affinity();
  void run_tasks_with_affinity(ThreadId with_affinity);

  void close();
  void close_and_wait();

 private:
  struct Task {
    template<typename ClosureType>
    Task(ClosureType&& closure, ThreadId affinity)
        : closure{std::forward<ClosureType>(closure)}, affinity{affinity} {}

    // The work item associated with this task. It is valid for the closure to
    // be empty, in which case the task simply acts as a dependency group.
    Closure closure;

    // The id of the 'next' task in a TaskList. Any task can be in a single
    // TaskList at a time.
    TaskId next_in_list{INVALID_TASK};

    // The beginning of the list of tasks which depend on this one. After this
    // task completes all of their num_unmet_dependencies counters will be
    // decremented.
    DependentId first_dependent{INVALID_DEPENDENT};

    // The number of tasks on which this one depends that haven't been ran yet.
    // The task will be ran when this reaches zero.
    DependencyCount num_unmet_dependencies{0};

    // If != NO_AFFINITY, the task will only be scheduled on the thread with
    // the corresponding ID.
    ThreadId affinity;

    // This stamp is a cycling task counter to enforce (approximate) ordering
    // between different task queues.
    TaskStamp stamp{0};
  };

  struct Dependent {
    inline Dependent(TaskId from, TaskId next) : from{from}, next{next} {}

    // The task which depends on the one whose dependent list this is.
    TaskId from;

    // The id of the next dependent in the list or INVALID_DEPENDENT if this is
    // the last element.
    DependentId next;
  };

  // A linked list of tasks.
  struct TaskList {
    TaskId front, back;
  };

  // Moves a previously waiting task onto an appropriate scheduled task queue.
  inline void schedule(TaskId id, Task& task);

  // Pushes a task at the front of a TaskList (FIFO).
  inline void push_task(TaskId id, Task& task, TaskList& queue);

  // Pops a task of a queue, removes it from the tasks_ vector and returns it.
  inline Task pop_task(TaskList& queue);

  // Runs a task and signals its dependents. Expects a locked unique_lock.
  inline void run_task(Task&& task, std::unique_lock<Mutex>& lock);

  // Decrements the unmet dependencies counter of all the dependents of a task
  // and schedules and tasks whose counter is zero.
  inline void signal_dependents(Task& task);

  // Is a task list empty?
  static inline bool empty_task_list(const TaskList& list);

  // An id vector of all the added tasks (all the tasklists refer to elements
  // in this vector).
  TaskIdVector tasks_;

  // An id vector of all dependencies, the dependent list nodes are stored in
  // this vector.
  DependentIdVector dependents_;

  // The scheduled tasks with no affinity.
  TaskList global_task_queue_{INVALID_TASK, INVALID_TASK};

  // The scheduled tasks with affinities.
  TaskList affinity_task_queue_[MAX_THREADS];

  // [i] == true if there exists a thread which is currently running
  // run_tasks(i) i.e. handling tasks with affinity == i.
  bool thread_exists_for_affinity_[MAX_THREADS]{false};

  // Mutex which protects access to all non-constant members.
  Mutex tasks_mutex_;

  // The stamp to give the next task; this is incremented on every new scheduled
  // task.
  TaskStamp next_stamp_{0};

  // Number of threads which are inside a task loop currently.
  BlockingCounter num_threads_;

  // Flag which is set to signal the threads to stop.
  bool closed_{false};
};

template <typename ClosureType, typename TaskIdRange>
inline Executor::TaskId Executor::add_task(ClosureType&& closure,
                                 const TaskIdRange& depends_on,
                                 ThreadId affinity) {
  return add_task(std::forward<ClosureType>(closure), depends_on.begin(),
                  depends_on.end(), affinity);
}

template <typename ClosureType, typename TaskIdFwdIter>
Executor::TaskId Executor::add_task(ClosureType&& closure,
                                    TaskIdFwdIter depends_begin,
                                    TaskIdFwdIter depends_end,
                                    ThreadId affinity) {
  if (closed_) return INVALID_TASK;

  DCHECK(affinity == NO_AFFINITY || affinity <= MAX_THREADS)
      << "Invalid affinity: " << affinity;

  std::lock_guard<Mutex> lock{tasks_mutex_};

  TaskId new_task_id =
      tasks_.emplace(std::forward<ClosureType>(closure), affinity);
  Task& new_task = tasks_[new_task_id];

  for (; depends_begin < depends_end; ++depends_begin) {
    TaskId dependency_id{*depends_begin};
    if (!tasks_.is_valid_id(dependency_id)) continue;

    Task& dependency = tasks_[dependency_id];
    dependency.first_dependent =
        dependents_.emplace(new_task_id, dependency.first_dependent);
    ++new_task.num_unmet_dependencies;
  }

  if (new_task.num_unmet_dependencies == 0) schedule(new_task_id, new_task);

  return new_task_id;
}

inline void Executor::schedule(TaskId id, Task& task) {
  DCHECK_EQ(task.num_unmet_dependencies, 0);

  task.stamp = next_stamp_++;
  if (task.affinity == NO_AFFINITY) {
    push_task(id, task, global_task_queue_);
  } else {
    push_task(id, task, affinity_task_queue_[task.affinity]);
  }
}

inline void Executor::push_task(TaskId id, Task& task, TaskList& queue) {
  task.next_in_list = queue.front;
  queue.front = id;
  if (queue.back == INVALID_TASK) queue.back = id;
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_EXECUTOR_HPP_
