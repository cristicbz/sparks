#include "unique_pulse.hpp"

#include <glog/logging.h>

namespace sparks {

void UniquePulse::wait() {
  std::unique_lock<std::mutex> lock{mutex_};
  DCHECK(!asleep_);
  asleep_ = true;
  while (!pulsed_) condition_.wait(lock);
  pulsed_ = false;
  asleep_ = false;
}

}  // namespace sparks
