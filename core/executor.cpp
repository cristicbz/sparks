#include "executor.hpp"

#include "id_vector.hpp"

#include <glog/logging.h>

namespace sparks {

Executor::Executor() : tasks_{64, 64} {
  //for (ThreadId i_queue = 0; i_queue < MAX_THREADS; ++i_queue) {
  //  per_thread_queue_[i_queue].first = INVALID_TASK;
  //  per_thread_queue_[i_queue].last = INVALID_TASK;
  //}
}

void Executor::stop() {
  std::unique_lock<std::mutex> lock{mutex_};
  if (stopped_) return;

  auto tasks_remaining = num_scheduled_tasks_.count();
  if (tasks_remaining > 0) {
    LOG(INFO) << num_scheduled_tasks_.count()
              << " tasks remaining, clearing queues...";
    clear_queue(main_queue_);
    for (ThreadId i_queue = 0; i_queue < MAX_THREADS; ++i_queue) {
      clear_queue(per_thread_queue_[i_queue]);
    }
  } else {
    LOG(INFO) << "No tasks reminaing.";
  }
  num_scheduled_tasks_.wait_and_disable();

  LOG(INFO) << "Signalling " << (active_threads_blocking_counter_.count() - 1)
            << " threads to stop.";
  stopped_ = true;
  num_sleeping_threads_ = 0;
  lock.unlock();
  not_empty_condition_.notify_all();

  LOG(INFO) << "Waiting on threads...";
  active_threads_blocking_counter_.wait_and_disable();

  // Check that cleanup was successful.
  CHECK_EQ(num_sleeping_threads_, 0);
  CHECK(empty(main_queue_));
  for (ThreadId i_queue = 0; i_queue < MAX_THREADS; ++i_queue) {
    CHECK(!handled_affinities[i_queue]) << i_queue;
    CHECK(empty(per_thread_queue_[i_queue])) << i_queue;
  }
  CHECK_EQ(tasks_.size(), 0);
  LOG(INFO) << "All post-conditions check out, finalising destruction. "
            << " A total of " << next_task_stamp_ << " tasks were ran, with "
            << "never more than " << tasks_.capacity() << " queued.";
}

Executor::~Executor() {
  num_scheduled_tasks_.wait_and_disable();
  stop();
}

void Executor::clear_queue(TaskList& queue) {
  uint32_t num_removed_tasks = 0;
  while (queue.first != INVALID_TASK) {
    DCHECK(tasks_.is_valid_id(queue.first));
    auto remove = queue.first;
    queue.first = tasks_[queue.first].next_task;
    tasks_.erase(remove);
    ++num_removed_tasks;
  }
  queue.last = INVALID_TASK;
  num_scheduled_tasks_.decrement(num_removed_tasks);
}

void Executor::pop_and_run(std::unique_lock<std::mutex>& lock,
                           TaskList& queue) {
  pop_and_run(lock, queue, tasks_[queue.first]);
}

void Executor::pop_and_run(std::unique_lock<std::mutex>& lock, TaskList& queue,
                           Task& task) {
  DCHECK(!empty(queue));
  DCHECK(tasks_.is_valid_id(queue.first));
  DCHECK(&tasks_[queue.first] == &task);

  // Move closure to a local variable then remove from task vector
  auto task_id = queue.first;
  auto next_task = task.next_task;
  auto closure = std::move(task.closure);
  tasks_.erase(task_id);

  // Pop id from task queue.
  queue.first = next_task;
  if (next_task == INVALID_TASK) queue.last = INVALID_TASK;

  // Unlock to allow other threads to run closures and run our closure.
  lock.unlock();
  closure();
  num_scheduled_tasks_.decrement();
}

bool Executor::run_one_no_affinity() {
  std::unique_lock<std::mutex> lock{mutex_};
  if (stopped_) return false;

  // Wait on condition variable for a new element.
  while (empty(main_queue_)) {
    ++num_sleeping_threads_;
    not_empty_condition_.wait(lock);
    if (stopped_) return false;
  }

  pop_and_run(lock, main_queue_);
  return true;
}

bool Executor::run_one_with_affinity(TaskList& thread_queue) {
  std::unique_lock<std::mutex> lock{mutex_};
  if (stopped_) return false;
  bool main_empty = empty(main_queue_);
  bool thread_empty = empty(thread_queue);

  // Wait on condition variable for a an element on either queue.
  while (main_empty && thread_empty) {
    ++num_sleeping_threads_;
    not_empty_condition_.wait(lock);
    if (stopped_) return false;

    main_empty = empty(main_queue_);
    thread_empty = empty(thread_queue);
  }

  if (!(main_empty || thread_empty)) {
    // If there's a new task on both the thread and main queue, pick the
    // one which was added first.
    DCHECK(tasks_.is_valid_id(main_queue_.first));
    DCHECK(tasks_.is_valid_id(thread_queue.first));
    auto& main_front = tasks_[main_queue_.first];
    auto& thread_front = tasks_[thread_queue.first];

    if (main_front.stamp < thread_front.stamp) {
      pop_and_run(lock, main_queue_, main_front);
    } else {
      pop_and_run(lock, thread_queue, thread_front);
    }
  } else if (!main_empty) {
    pop_and_run(lock, main_queue_);
  } else if (!thread_empty) {
    pop_and_run(lock, thread_queue);
  }

  return true;
}

void Executor::run_tasks(ThreadId with_affinity) {
  std::unique_lock<std::mutex> lock{mutex_};
  if (stopped_) return;
  BlockingCounter::Item item{active_threads_blocking_counter_};
  CHECK_LE(active_threads_blocking_counter_.count() - 1, MAX_THREADS);
  lock.unlock();
  if (with_affinity == NO_AFFINITY) {
    while (run_one_no_affinity()) {
      // Do nothing.
    }
  } else {
    CHECK_LT(with_affinity, MAX_THREADS);
    CHECK(!handled_affinities[with_affinity]);
    handled_affinities[with_affinity] = true;
    auto& thread_queue = per_thread_queue_[with_affinity];
    while (run_one_with_affinity(thread_queue)) {
      // Do nothing.
    }
    handled_affinities[with_affinity] = false;
  }
}

void Executor::add_task(Closure closure, ThreadId affinity) {
  DCHECK(closure);
  std::unique_lock<std::mutex> lock{mutex_};
  if (stopped_) return;

  auto id = tasks_.emplace(std::move(closure), next_task_stamp_++);

  Task& task = tasks_[id];
  if (affinity == NO_AFFINITY) {
    add_to_queue(main_queue_, id, task);
    if (num_sleeping_threads_ > 0) {
      --num_sleeping_threads_;
      lock.unlock();
      not_empty_condition_.notify_one();
    }
  } else {
    DCHECK_LT(affinity, MAX_THREADS);
    LOG_IF(WARNING, !handled_affinities[affinity])
        << "Task with unhandled affinity " << affinity << " added.";
    add_to_queue(per_thread_queue_[affinity], id, task);
    if (num_sleeping_threads_ > 0) {
      num_sleeping_threads_ = 0;
      lock.unlock();
      // We notify ALL, but only the corresponding thread will pull it from
      // its own queue, all others will go back to sleep.
      not_empty_condition_.notify_all();
    }
  }
}

void Executor::add_to_queue(TaskList& queue, TaskId id, Task& task) {
  DCHECK(tasks_.is_valid_id(id));
  DCHECK(&tasks_[id] == &task);
  if (empty(queue)) {
    queue.first = id;
  } else {
    tasks_[queue.last].next_task = id;
  }
  queue.last = id;
  task.next_task = INVALID_TASK;
  num_scheduled_tasks_.increment(1);
}

bool Executor::empty(TaskList& queue) {
  DCHECK_EQ(queue.first == INVALID_TASK, queue.last == INVALID_TASK);
  return queue.first == INVALID_TASK;
}

}  // namespace sparks
