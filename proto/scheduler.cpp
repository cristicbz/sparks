#include "scheduler.hpp"

namespace sparks {

Scheduler::Scheduler(size_t num_nodes) {
  nodes_ = new SchedulerNode[num_nodes_ = num_nodes];
}

Scheduler::~Scheduler() { delete[] nodes_; }

void Scheduler::run_tasks_loop_with_task_id(TaskId root_id) {
  const auto num_additional_threads = num_nodes() - 1;
  std::vector<std::thread> threads;
  threads.reserve(num_additional_threads);

  for (NodeId i_node = 0; i_node < num_nodes_; ++i_node) {
    node(i_node).attach(*this, i_node);
  }

  for (NodeId i_thread = 0; i_thread < num_additional_threads; ++i_thread) {
    threads.emplace_back([this, i_thread]{
      node(i_thread + 1).run_tasks_loop();
    });
  }

  auto x = SDL_GetPerformanceCounter();
  node(0).generic_tasks_.unique_push(root_id);
  node(0).run_tasks_loop();
  LOG(INFO) << ((SDL_GetPerformanceCounter() - x) /
                double(SDL_GetPerformanceFrequency()));
  for (auto& thread : threads) thread.join();
}


void SchedulerNode::attach(Scheduler& scheduler, NodeId this_id) {
  CHECK(!scheduler_);
  scheduler_ = &scheduler;
  this_id_ = this_id;
  steal_from_.store(local_next_node());
}

void SchedulerNode::terminate() {
  CHECK(scheduler_ != nullptr);
  if (!stop_flag_.exchange(true)) {
    DLOG << "Stopping scheduler...";
    for (NodeId i_node = 0; i_node < scheduler_->num_nodes(); ++i_node) {
      scheduler_->node(i_node).foreign_stop_local();
    }
  }
}

void SchedulerNode::run_tasks_loop() {
  CHECK(scheduler_);
  CHECK(this_id_ != INVALID_NODE);
  auto num_nodes = scheduler_->num_nodes();
  uint32_t num_empty_runs = 0;
  auto steal_id = steal_from_.load();
  DLOG << id() << ": Node running. Depleting initial queue...";
  local_deplete_queue();
  available_.store(true);
  while (!stop_flag_.load()) {
    do {
      steal_id = (steal_id + 1) % num_nodes;
    } while (steal_id == this_id_);

    if (local_steal_and_execute(steal_id)) {
      if (!available_.exchange(false, std::memory_order_acq_rel)) {
        auto new_steal_id = steal_from_.load(std::memory_order_acquire);
        if (steal_id != new_steal_id && local_steal_and_execute(new_steal_id)) {
          steal_id = new_steal_id;
          DLOG << id() << ": Dealing with interleaved wakeup from "
                    << steal_id;
        }
      }
      num_empty_runs = 0;
    } else if ((num_empty_runs += 1) == num_nodes) {
      DLOG << id() << ": Enough empty runs, sleeping...";
      wakeup_.wait();
      steal_id = steal_from_.load(std::memory_order_acquire);
      DLOG << id() << ": Woken up by " << steal_id << ". Doing task..";
      local_steal_and_execute(steal_id);
      DLOG << id() << ": Going back to stealing.";
      num_empty_runs = 0;
    } else {
      continue;
    }

    TaskId pulled_id;
    while (generic_tasks_.unique_pull(pulled_id)) local_execute(pulled_id);
    available_.store(true, std::memory_order_release);
  }
}

void SchedulerNode::foreign_stop_local() {
  stop_flag_.store(true);
  wakeup_.pulse();
}

Scheduler::NodeId SchedulerNode::local_next_node() const {
  return (this_id_ + 1) % scheduler_->num_nodes();
}

bool SchedulerNode::foreign_wakeup_and_steal_from(NodeId from) {
  if (available_.exchange(false, std::memory_order_acq_rel)) {
    steal_from_.store(from, std::memory_order_release);
    wakeup_.pulse();
    return true;
  }
  return false;
}

void SchedulerNode::local_deplete_queue() {
  TaskId task_id;
  while (generic_tasks_.unique_pull(task_id)) {
    DLOG << id() << ": Executing own task.";
    local_execute(task_id);
  }
}


bool SchedulerNode::local_steal_and_execute(NodeId from) {
  TaskId task_id;
  if (scheduler_->node(from).generic_tasks_.shared_pull(task_id)) {
    DLOG << id() << ": Stole task from " << from;
    local_execute(task_id);
    return true;
  }
  return false;
}

void SchedulerNode::local_try_delegate(TaskId new_task_id) {
  auto num_nodes = scheduler_->num_nodes();
  auto this_id = this->this_id_;
  auto i_node = this_id;
  while ((i_node = (i_node + 1) % num_nodes) != this_id) {
    if (scheduler_->node(i_node).foreign_wakeup_and_steal_from(this_id)) {
      DLOG << id() << ": Explicitly delegated task to " << i_node;
      return;
    }
  }
}

}  // namespace sparks
