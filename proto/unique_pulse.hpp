#ifndef SPARKS_CORE_UNIQUE_PULSE_HPP_
#define SPARKS_CORE_UNIQUE_PULSE_HPP_

#include <mutex>
#include <condition_variable>

namespace sparks {

class UniquePulse {
 public:
  inline void pulse();
  void wait();

 private:
  bool pulsed_{false};
  bool asleep_{false};
  std::condition_variable condition_;
  std::mutex mutex_;
};

void UniquePulse::pulse() {
  std::unique_lock<std::mutex> lock{mutex_};
  pulsed_ = true;
  if (asleep_) {
    asleep_ = false;
    lock.unlock();
    condition_.notify_one();
  }
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_UNIQUE_PULSE_HPP_
