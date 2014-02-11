#ifndef SPARKS_CORE_BLOCKING_QUEUE_FWD_HPP_
#define SPARKS_CORE_BLOCKING_QUEUE_FWD_HPP_

#include <condition_variable>
#include <mutex>

#include "bounded_queue.hpp"

namespace sparks  {

template<class ElemType>
class BlockingQueue {
 public:
  typedef ElemType value_type;

  explicit BlockingQueue(size_t capacity) : queue_{capacity} {}

  inline bool push(value_type value);
  inline bool pop(value_type& value);
  inline void cancel();

  bool cancelled() const { return cancelled_; }

 private:
  std::mutex mutex_;
  std::condition_variable not_empty_condition_;
  std::condition_variable not_full_condition_;
  BoundedQueue<value_type> queue_;

  bool cancelled_ = false;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_BLOCKING_QUEUE_FWD_HPP_
