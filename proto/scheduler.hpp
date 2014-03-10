#ifndef SPARKS_CORE_SCHEDULER_HPP_
#define SPARKS_CORE_SCHEDULER_HPP_

#include "id_vector.hpp"
#include "work_stealing_queue.hpp"
#include "arraydelegate.hpp"

#include <boost/lockfree/stack.hpp>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <SDL2/SDL.h>

#undef DLOG
#define DLOG if(false) LOG(INFO)

namespace sparks {

class Scheduler;
class SchedulerNode;
struct Task;

class UniquePulse {
 public:
  void pulse() {
    std::unique_lock<std::mutex> lock{mutex_};
    pulsed_ = true;
    if (asleep_) {
      asleep_ = false;
      lock.unlock();
      condition_.notify_one();
    }
  }

  void wait() {
    std::unique_lock<std::mutex> lock{mutex_};
    CHECK(!asleep_);
    asleep_ = true;
    while (!pulsed_) condition_.wait(lock);
    pulsed_ = false;
    asleep_ = false;
  }

 private:
  bool pulsed_{false};
  bool asleep_{false};
  std::condition_variable condition_;
  mutable std::mutex mutex_;
};


class Scheduler {
 public:
  using NodeId = uint16_t;

  static const size_t SCHEDULER_MAX_NODES = 32;
  static const size_t SCHEDULER_MAX_UNSCHEDULED_TASKS_BITS = 24;
  static const NodeId NO_AFFINITY = static_cast<NodeId>(-1);
  static const NodeId INVALID_NODE = static_cast<NodeId>(-1);

 private:
  struct Task;
  using TaskVector =
    BasicIdVector<Task, uint32_t, SCHEDULER_MAX_UNSCHEDULED_TASKS_BITS>;

 public:
  friend class SchedulerNode;

  using TaskId = TaskVector::Id;

  static const TaskId INVALID_TASK = TaskVector::INVALID_ID;

  explicit Scheduler(size_t num_nodes);
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;

  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  template <class Function>
  void run(Function&& root);

  inline SchedulerNode& get_node(NodeId node_id);

  size_t num_nodes() const { return num_nodes_; }

 private:
  using WorkItem = sparks::arraydelegate<void(SchedulerNode&)>;

  struct Task {
    template <class Function>
    Task(Function&& iwork) : work{std::forward<Function>(iwork)} {}

    WorkItem work;
  };

  Task& get_task(TaskId id) { return tasks_[id]; }

  void erase_task(TaskId id) { tasks_.erase(id); }

  SchedulerNode* nodes_;
  size_t num_nodes_;
  TaskVector tasks_;
};


class SchedulerNode {
 public:
  friend class Scheduler;

  using TaskId = Scheduler::TaskId;
  using NodeId = Scheduler::NodeId;

  static const TaskId INVALID_TASK = Scheduler::INVALID_TASK;
  static const NodeId INVALID_NODE = Scheduler::INVALID_NODE;
  static const NodeId NO_AFFINITY = Scheduler::NO_AFFINITY;

  SchedulerNode() {}

  void attach(Scheduler& scheduler, NodeId this_id);

  SchedulerNode(const SchedulerNode&) = delete;
  SchedulerNode(SchedulerNode&&) = delete;

  SchedulerNode& operator=(const SchedulerNode&) = delete;
  SchedulerNode& operator=(SchedulerNode&&) = delete;

  template <class Function>
  void new_task(Function&& work);

  void node_loop();
  void stop_scheduler();

  NodeId get_id() const { return this_id_; }

 private:
  using WorkItem = Scheduler::WorkItem;
  using Task = Scheduler::Task;

  static const size_t SCHEDULER_MAX_SCHEDULED_TASKS_BITS = 8;
  static const size_t SCHEDULER_MAX_AFFINE_TASKS_BITS = 8;

  using TaskStealingQueue =
    WorkStealingQueue<TaskId, SCHEDULER_MAX_SCHEDULED_TASKS_BITS>;

  bool wakeup_and_steal_from(NodeId from) {
    if (available_.exchange(false, std::memory_order_acq_rel)) {
      steal_from_.store(from, std::memory_order_release);
      wakeup_.pulse();
      return true;
    }
    return false;
  }

  void stop() {
    stop_flag_.store(true);
    wakeup_.pulse();
  }

  NodeId next_node() const { return (this_id_ + 1) % scheduler_->num_nodes(); }

  void execute(TaskId task_id) {
    Task& task = scheduler_->get_task(task_id);
    task.work(*this);
    scheduler_->erase_task(task_id);
  }

  inline void deplete_local_queue();

  bool steal_and_execute(NodeId from) {
    TaskId task_id;
    if (scheduler_->get_node(from).generic_tasks_.shared_pull(task_id)) {
      DLOG << get_id() << ": Stole task from " << from;
      execute(task_id);
      return true;
    }
    return false;
  }

  Scheduler* scheduler_{nullptr};
  NodeId this_id_{INVALID_NODE};
  TaskStealingQueue generic_tasks_;
  UniquePulse wakeup_;
  std::atomic<bool> stop_flag_{false};
  std::atomic<bool> available_{false};
  std::atomic<NodeId> steal_from_{INVALID_NODE};
};

Scheduler::Scheduler(size_t num_nodes) {
  nodes_ = new SchedulerNode[num_nodes_ = num_nodes];
}

Scheduler::~Scheduler() { delete[] nodes_; }

SchedulerNode& Scheduler::get_node(NodeId node_id) { return nodes_[node_id]; }

template <class Function>
void Scheduler::run(Function&& root) {
  std::vector<std::thread> threads;
  auto num_additional_threads = num_nodes() - 1;
  threads.reserve(num_additional_threads);

  for (int i_node = 0; i_node < num_nodes_; ++i_node) {
    get_node(i_node).attach(*this, i_node);
  }

  for (int i_thread = 0; i_thread < num_additional_threads; ++i_thread) {
    threads.emplace_back([this, i_thread]{
      get_node(i_thread + 1).node_loop();
    });
  }

  auto x = SDL_GetPerformanceCounter();
  get_node(0).new_task(root);
  get_node(0).node_loop();

  for (auto& thread : threads) thread.join();
  LOG(INFO) << ((SDL_GetPerformanceCounter() - x) / double(SDL_GetPerformanceFrequency()));
}

void SchedulerNode::stop_scheduler() {
  CHECK(scheduler_ != nullptr);
  if (!stop_flag_.exchange(true)) {
    DLOG << "Stopping scheduler...";
    for (int i_node = 0; i_node < scheduler_->num_nodes(); ++i_node) {
      scheduler_->get_node(i_node).stop();
    }
  }
}

inline void SchedulerNode::deplete_local_queue() {
  TaskId task_id;
  while (generic_tasks_.unique_pull(task_id)) {
    DLOG << get_id() << ": Executing own task.";
    execute(task_id);
  }
}

template <class Function>
void SchedulerNode::new_task(Function&& work) {
  TaskId new_task_id;
  Task* new_task;
  std::tie(new_task_id, new_task) = scheduler_->tasks_.emplace(std::forward<Function>(work));
  DCHECK(new_task != nullptr);

  while (!generic_tasks_.unique_push(new_task_id))  {
    TaskId pulled_id;
    if (generic_tasks_.unique_pull(pulled_id)) execute(pulled_id);
  }

  // Try to delegate the task to another node if there's more than one task on
  // the queue.
  auto num_nodes = scheduler_->num_nodes();
  auto this_id = this->this_id_;
  for (NodeId i_node = (this_id + 1) % num_nodes;
       generic_tasks_.size() > 1 && i_node != this_id;
       i_node = (i_node + 1) % num_nodes) {
    if (scheduler_->get_node(i_node).wakeup_and_steal_from(this_id)) {
      DLOG << get_id() << ": Explicitly delegated task to " << i_node;
      return;
    }
  }
}

void SchedulerNode::attach(Scheduler& scheduler, NodeId this_id) {
  CHECK(!scheduler_);
  scheduler_ = &scheduler;
  this_id_ = this_id;
  steal_from_.store(next_node());
}

void SchedulerNode::node_loop() {
  CHECK(scheduler_);
  CHECK(this_id_ != INVALID_NODE);
  auto num_nodes = scheduler_->num_nodes();
  uint32_t num_empty_runs = 0;
  auto steal_id = steal_from_.load();
  DLOG << get_id() << ": Node running. Depleting initial queue...";
  deplete_local_queue();
  available_.store(true);
  while (!stop_flag_.load()) {
    do {
      steal_id = (steal_id + 1) % num_nodes;
    } while (steal_id == this_id_);

    if (steal_and_execute(steal_id)) {
      if (!available_.exchange(false, std::memory_order_acq_rel)) {
        auto new_steal_id = steal_from_.load(std::memory_order_acquire);
        if (steal_id != new_steal_id && steal_and_execute(new_steal_id)) {
          steal_id = new_steal_id;
          DLOG << get_id() << ": Dealing with interleaved wakeup from "
                    << steal_id;
        }
      }
      num_empty_runs = 0;
    } else if ((num_empty_runs += 1) == num_nodes) {
      DLOG << get_id() << ": Enough empty runs, sleeping...";
      wakeup_.wait();
      steal_id = steal_from_.load(std::memory_order_acquire);
      DLOG << get_id() << ": Woken up by " << steal_id << ". Doing task..";
      steal_and_execute(steal_id);
      DLOG << get_id() << ": Going back to stealing.";
      num_empty_runs = 0;
    } else {
      continue;
    }

    TaskId pulled_id;
    while (generic_tasks_.unique_pull(pulled_id)) execute(pulled_id);
    available_.store(true, std::memory_order_release);
  }
}


}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_SCHEDULER_HPP_

