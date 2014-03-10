#ifndef SPARKS_CORE_SIGNAL_HPP_
#define SPARKS_CORE_SIGNAL_HPP_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include <glog/logging.h>

namespace sparks {
class Signal {
 public:
  static const uint32_t SPIN_COUNT{8};

  ~Signal() {
    CHECK_EQ(num_waiting_threads_, 0);
  }

  void trigger() {
    std::unique_lock<std::mutex> lock{mutex_};
    if (!triggered_ && num_waiting_threads_ > 0) {
      triggered_.store(true);
      lock.unlock();
      condition_variable_.notify_all();
    }
  }

  void reset() {
    std::unique_lock<std::mutex> lock{mutex_};
    CHECK_EQ(num_waiting_threads_, 0);
    CHECK(triggered_);
    triggered_.store(false);
  }

  void wait() {
    uint32_t cycles_ = SPIN_COUNT + 1;
    while ((--cycles_) > 0) {
      if (!triggered_.load()) return;
    }

    std::unique_lock<std::mutex> lock{mutex_};
    while (!triggered_) {
      ++ num_waiting_threads_;
      condition_variable_.wait(lock);
      -- num_waiting_threads_;
    }
  }

 private:
  uint32_t num_waiting_threads_{0};
  std::atomic<bool> triggered_{false};
  std::mutex mutex_;
  std::condition_variable condition_variable_;
};

}  // namespace sparks


#endif  // #ifndef SPARKS_CORE_SIGNAL_HPP_
