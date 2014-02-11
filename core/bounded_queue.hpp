#ifndef SPARKS_CORE_BOUNDED_QUEUE_HPP_
#define SPARKS_CORE_BOUNDED_QUEUE_HPP_

#include <memory>

#include <glog/logging.h>

namespace sparks {

template<typename ElemType, typename Allocator = std::allocator<ElemType>>
class BoundedQueue {
 public:
   using allocator_type = Allocator;
   using size_type = typename allocator_type::size_type;
   using value_type = typename allocator_type::value_type;
   using reference = typename allocator_type::reference;
   using pointer = typename allocator_type::pointer;
   using const_reference = typename allocator_type::const_reference;
   using const_pointer = typename allocator_type::const_pointer;

   BoundedQueue()
       : begin_{nullptr}, end_{nullptr}, first_{nullptr}, last_{nullptr},
         size_{0} {}

   explicit BoundedQueue(size_type capacity,
                const allocator_type &allocator = allocator_type{})
       : size_{0}, allocator_{allocator} {
     CHECK_GE(capacity, 0);
     CHECK_LE(capacity, allocator_.max_size());
     begin_ = (capacity == 0) ? nullptr : allocator_.allocate(capacity, 0);
     end_ = begin_ + capacity;
     first_ = last_ = begin_;
   }

   BoundedQueue(const BoundedQueue& other) = delete;
   BoundedQueue(BoundedQueue&& other)
     : allocator_{std::move(other.allocator_)},
       begin_{other.begin_}, end_{other.end_}, first_{other.first_}, last_{other.last_},
       size_{other.size_} {
     other.begin_ = other.end_ = other.first_ = other.last_ = nullptr;
     other.size_ = 0;
   }

   ~BoundedQueue() {
     if (begin_) allocator_.deallocate(begin_, capacity());
   }

   size_type capacity() const { return end_ - begin_; }
   size_type size() const { return size_; }
   bool empty() const { return size_ == 0; }
   bool full() const { return size_ == capacity(); }

   reference front() { DCHECK(!empty()); return *first_; }
   const_reference front() const { DCHECK(!empty()); return *first_; }

   void push(value_type element) {
     DCHECK(!full());
     emplace(std::move(element));
   }

   void pop() {
     DCHECK(!empty());
     allocator_.destroy(*first_);
     if (++first_ == end_) first_ = begin_;
     --size_;
   }

   template<typename ...Args>
   bool emplace(Args&& ...args) {
     ::new (last_) value_type(std::forward<Args...>(args...));
     if (++last_ == end_) last_ = begin_;
     ++size_;
   }

   void swap(BoundedQueue& other) {
     using std::swap;
     swap(allocator_, other.allocator_);
     swap(begin_, other.begin_);
     swap(end_, other.end_);
     swap(first_, other.first_);
     swap(last_, other.last_);
     swap(size_, other.size_);
   }

 private:
   allocator_type allocator_;
   pointer begin_, end_, first_, last_;
   size_type size_;
};


}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_BOUNDED_QUEUE_HPP_
