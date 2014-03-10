#include "scheduler.hpp"
#include <SDL2/SDL.h>

namespace {
  std::atomic<int> tasks_left{0};
  int example = 0;
}

bool task_done() {
  auto tlm = tasks_left.fetch_sub(1, std::memory_order_acq_rel);
  return (tlm == 1);
}

int main() {
  google::InitGoogleLogging("sparks");
  google::LogToStderr();
  sparks::Scheduler scheduler{4};

  tasks_left.store(64000);
  scheduler.run([] (sparks::SchedulerNode& node){
    //LOG(INFO) << "-> " << node.get_id() << ": Root task.";
    for (int i = 0; i < 40; ++i) {
      node.new_task([i] (sparks::SchedulerNode& node) {
        //LOG(INFO) << "-> " << node.get_id() << ": Child task " << i;
        for (int j = 0; j < 40; ++j) {
          node.new_task([i, j] (sparks::SchedulerNode& node) {
            //LOG(INFO) << "-> " << node.get_id() << ": Subchild task " << i << "." << j;
            for (int k = 0; k < 40; ++k) {
              node.new_task([i, j, k] (sparks::SchedulerNode& node) {
                //LOG_EVERY_N(INFO, 1000000) << google::COUNTER;
                //LOG(INFO) << "-> " << node.get_id() << ": Subchild task " << i << "." << j;
                for (int m = 0; m < 1000; ++m)
                  for (int n = 0; n < 1000; ++n)
                    example += tasks_left.load(std::memory_order_relaxed);

                if (task_done()) node.stop_scheduler();
              });
            }
          });
        }
      });
    }
  });
  LOG(INFO) << example;
  return 0;
}
