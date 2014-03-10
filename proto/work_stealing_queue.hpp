#ifndef SPARKS_CORE_WORK_STEALING_QUEUE_HPP_
#define SPARKS_CORE_WORK_STEALING_QUEUE_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>

namespace sparks {

template <class Element_, size_t CAPACITY_BITS, typename Size_ = uint32_t>
class WorkStealingQueue {
 public:
  using Element = Element_;
  using Size = Size_;

  static const Size CAPACITY{1uL << CAPACITY_BITS};

  static_assert(std::is_pod<Element_>::value, "work queue type is non-POD");
  static_assert(CAPACITY > 0, "zero capacity");
  static_assert(static_cast<Size>(CAPACITY) == CAPACITY,
                "capacity incompatible with Size");

 private:
  using Mutex = std::timed_mutex;
  using LockGuard = std::lock_guard<Mutex>;
  using UniqueLock = std::unique_lock<Mutex>;
  using AtomicIdx = std::atomic<Size>;

  static const Size MASK = CAPACITY - 1;

 public:
  WorkStealingQueue() : elements_{new Element[CAPACITY]} {}
  ~WorkStealingQueue() { delete[] elements_; }

  WorkStealingQueue(const WorkStealingQueue&) = delete;
  WorkStealingQueue(WorkStealingQueue&&) = delete;

  WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;
  WorkStealingQueue& operator=(WorkStealingQueue&&) = delete;

  bool empty() const { return head_ >= tail_; }
  Size size() const { return tail_ - head_; }

  bool unique_push(Element new_value) {
    auto tail_mirror = tail_.load();
    if (tail_mirror < head_.load() + MASK) {
      elements_[tail_mirror & MASK] = new_value;
      tail_.store(tail_mirror + 1);
      return true;
    } else {
      return false;
    }
  }

  bool unique_pull(Element& to) {
    auto tail_mirror = tail_.load();
    if (head_.load() >= tail_mirror) return false;

    tail_.store(--tail_mirror);
    if (head_ <= tail_mirror) {
      to = elements_[tail_mirror & MASK];
      return true;
    } else {
      LockGuard guard{foreign_sync_};
      if (head_.load() <= tail_mirror) {
        to = elements_[tail_mirror & MASK];
        return true;
      } else {
        tail_.store(tail_mirror + 1);
        return false;
      }
    }
  }

  template<typename TimeoutDuration = std::chrono::milliseconds>
  bool shared_pull(Element& to, const TimeoutDuration& timeout =
                                    std::chrono::milliseconds{0}) {
    UniqueLock lock{foreign_sync_, timeout};
    if (!lock) return false;
    auto head_mirror = head_.load();
    head_.store(head_mirror + 1);
    if (head_mirror < tail_.load()) {
      to = elements_[head_mirror & MASK];
      return true;
    } else {
      head_.store(head_mirror);
      return false;
    }
  }

 private:
  Element *elements_;
  AtomicIdx head_{0};
  AtomicIdx tail_{0};
  Mutex foreign_sync_;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_WORK_STEALING_QUEUE_
