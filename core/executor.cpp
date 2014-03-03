#include "executor.hpp"

#include <thread>
#include <glog/logging.h>

namespace sparks {

Executor::Executor() : tasks_{16}, dependents_{16} {
  // Initialise affinity task queues to empty task lists.
  for (unsigned i = 0; i < MAX_THREADS; ++i) {
    affinity_task_queue_[i].front = affinity_task_queue_[i].back = INVALID_TASK;
    thread_exists_for_affinity_[i] = false;
  }
}

Executor::~Executor() {
  close_and_wait();
}

void Executor::run_tasks_no_affinity() {
  if (closed_) return;

  std::unique_lock<Mutex> lock{tasks_mutex_};
  BlockingCounter::Item running_thread{num_threads_};

  while (true) {
    while (empty_task_list(global_task_queue_)) {
      lock.unlock();
      std::this_thread::yield();
      lock.lock();
      if (closed_) {
        return;
      }
    }

    run_task(pop_task(global_task_queue_), lock);
  }
}

void Executor::run_tasks_with_affinity(ThreadId affinity) {
  if (closed_) return;
  if (affinity == NO_AFFINITY) {
    run_tasks_no_affinity();
    return;
  }

  std::unique_lock<Mutex> lock{tasks_mutex_};
  BlockingCounter::Item running_thread{num_threads_};

  CHECK(!thread_exists_for_affinity_[affinity]) << affinity;
  thread_exists_for_affinity_[affinity] = true;

  while (true) {
    TaskList& affinity_queue = affinity_task_queue_[affinity];
    bool global_is_empty = empty_task_list(global_task_queue_);
    bool affinity_is_empty = empty_task_list(affinity_queue);
    int start_sleeping{128};
    while (global_is_empty && affinity_is_empty) {
      lock.unlock();
      if (--start_sleeping < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } else {
        std::this_thread::yield();
      }
      lock.lock();

      if (closed_) {
        thread_exists_for_affinity_[affinity] = false;
        return;
      }

      global_is_empty = empty_task_list(global_task_queue_);
      affinity_is_empty = empty_task_list(affinity_queue);
    }

    if (!(global_is_empty || affinity_is_empty)) {
      // If both queues have tasks, pick the older one based on its stamp.
      if (tasks_[global_task_queue_.front].stamp <
          tasks_[affinity_queue.front].stamp) {
        run_task(pop_task(global_task_queue_), lock);
      } else {
        run_task(pop_task(affinity_queue), lock);
      }
    } else if(global_is_empty) {
      run_task(pop_task(affinity_queue), lock);
    } else {
      run_task(pop_task(global_task_queue_), lock);
    }
  }
}

void Executor::close() {
  std::lock_guard<Mutex> lock{tasks_mutex_};
  if (closed_) return;
  closed_ = true;
}

void Executor::close_and_wait() {
  close();
  num_threads_.wait_and_disable();
}


inline bool Executor::empty_task_list(const TaskList& list) {
  DCHECK(list.front != INVALID_TASK || list.back == INVALID_TASK);
  return list.front == INVALID_TASK;
}

Executor::Task Executor::pop_task(
    TaskList& queue) {
  DCHECK_NE(queue.front, INVALID_TASK);
  DCHECK_NE(queue.back, INVALID_TASK);

  TaskId task_id = queue.front;
  Task task{std::move(tasks_[task_id])};

  queue.front = task.next_in_list;
  if (queue.front == INVALID_TASK) queue.back = INVALID_TASK;

  tasks_.erase(task_id);
  return task;
}

void Executor::run_task(Task&& task, std::unique_lock<Mutex>& lock) {
  DCHECK_EQ(task.num_unmet_dependencies, 0);

  if (task.closure) {
    lock.unlock();
    task.closure();
    lock.lock();
  }

  signal_dependents(task);
}

void Executor::signal_dependents(Task& task) {
  DependentId dependent_id = task.first_dependent;

  while (dependent_id != INVALID_DEPENDENT) {
    Dependent& dependent = dependents_[dependent_id];
    Task& dependent_task = tasks_[dependent.from];
    DCHECK_NE(dependent_task.num_unmet_dependencies, 0);
    if ((--dependent_task.num_unmet_dependencies) == 0) {
      schedule(dependent.from, dependent_task);
    }

    DependentId next_dependent_id = dependent.next;
    dependents_.erase(dependent_id);
    dependent_id = next_dependent_id;
  }
}

}  // namespace sparks
