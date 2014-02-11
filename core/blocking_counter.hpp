#ifndef SPARKS_CORE_BLOCKING_COUNTER_HPP_
#define SPARKS_CORE_BLOCKING_COUNTER_HPP_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <type_traits>

#include <glog/logging.h>

namespace sparks {

class BlockingCounter {
 public:
  class Item;
  friend class Item;
  class Item {
   public:
    explicit Item(BlockingCounter& counter) : counter_{&counter} {
      CHECK(counter_->alive_);
      counter_->increment();
    }

    Item(const Item& other) : counter_{other.counter_} {
      if (counter_) counter_->increment();
    }

    Item(Item&& other) : counter_{other.counter_} { other.counter_ = nullptr; }

    ~Item() { release(); }

    Item& operator=(Item other) {
      swap(other);
      return *this;
    }

    void release() {
      if (counter_) {
        counter_->decrement();
        counter_ = nullptr;
      }
    }

    void swap(Item& other) { std::swap(counter_, other.counter_); }

   private:
    BlockingCounter* counter_;
  };

  BlockingCounter() = default;
  ~BlockingCounter() { wait_and_disable(); }

  void wait_and_disable() {
    if (alive_.exchange(false)) {
      std::unique_lock<std::mutex> lock{count_mutex_};
      --count_;
      while (count_ > 0) zero_count_.wait(lock);
    }
  }

  void increment(uint32_t by = 1) {
    count_ += by;
  }

  void decrement(uint32_t by = 1) {
    std::unique_lock<std::mutex> lock{count_mutex_};
    if (by >= count_) {
      count_ = 0;
      lock.unlock();
      zero_count_.notify_all();
    } else {
      count_ -= by;
    }
  }

  uint32_t count() const { return count_; }

 private:

  std::atomic<bool> alive_{true};
  uint32_t count_{1};
  std::mutex count_mutex_;
  std::condition_variable zero_count_;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_BLOCKING_COUNTER_HPP_
