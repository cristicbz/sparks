#ifndef SPARKS_CORE_BLOCKING_QUEUE_HPP_
#define SPARKS_CORE_BLOCKING_QUEUE_HPP_

#include "blocking_queue_fwd.hpp"

#include <glog/logging.h>

namespace sparks  {

template <class T>
bool BlockingQueue<T>::push(value_type value) {
  {
    std::unique_lock<std::mutex> lock{mutex_};
    if (cancelled_) return false;

    while (queue_.full()) {
      not_full_condition_.wait(lock);
      if (cancelled_) return false;
    }
    queue_.emplace(std::move(value));
  }
  not_empty_condition_.notify_one();
  return true;
}

template <class T>
bool BlockingQueue<T>::pop(value_type& value) {
  {
    std::unique_lock<std::mutex> lock{mutex_};
    if (cancelled_) return false;

    while (queue_.empty()) {
      not_empty_condition_.wait(lock);
      if (cancelled_) return false;
    }
    value = std::move(queue_.front());
    queue_.pop();
  }
  not_full_condition_.notify_one();
  return true;
}

template <class T>
void BlockingQueue<T>::cancel() {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (cancelled_) return;

    cancelled_ = true;
    BlockingQueue<value_type>().swap(queue_);
  }
  not_empty_condition_.notify_all();
  not_full_condition_.notify_all();
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_BLOCKING_QUEUE_HPP_
