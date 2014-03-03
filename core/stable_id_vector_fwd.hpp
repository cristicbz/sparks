#ifndef SPARKS_CORE_SHARED_ID_VECTOR_HPP_
#define SPARKS_CORE_SHARED_ID_VECTOR_HPP_

#include <cstdint>
#include <type_traits>
#include <vector>

namespace sparks {

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
class BasicSharedIdVector {
 public:
  using Id = IdType;
  using size_type = IdType;
  using value_type = ElemType;
  using reference_type = ElemType&;
  using pointer_type = ElemType*;

  static_assert(OUTER_BITS < sizeof(IdType) * 8,
                "There needs to be at least one inner bit.");

  static const Id INNER_BITS = sizeof(IdType) - OUTER_BITS;
  static const Id OUTER_MASK = (1 << OUTER_BITS) - 1;
  static const Id INNER_MASK = ~OUTER_MASK;
  static const Id MAX_INDEX = OUTER_MASK - 1;
  static const Id INVALID_INDEX = OUTER_MASK;
  static const size_type MAX_SIZE = MAX_INDEX + 1;

  BasicSharedIdVector() {
    entries_ = new Entry[MAX_SIZE];
    for (Id i = 0; i < MAX_SIZE; ++i) {
      entries_[i].id = i + 1;
    }
    first_free_ = 0;
  }

  ~BasicSharedIdVector() {
    for (IdType i = 0; i < MAX_SIZE; ++i) {
      if ((entries_[i].id & OUTER_MASK) == i) {
        reinterpret_cast<value_type*>(entries_[i].data)->~value_type();
      }
    }
  }

  bool is_valid_id(Id id) const {
    auto outer_id = id & OUTER_MASK;
    if (id > MAX_SIZE) return false;
    return entries_[outer_id].id == id;
  }

  template<typename ...Args>
  inline Id emplace(Args&& ...args);

  inline void erase(Id id);
  inline value_type& operator[](Id id);
  inline const value_type& operator[](Id id) const;

 private:
  using InnerId = typename std::conditional<INNER_BITS <=  8,  uint8_t,
                  typename std::conditional<INNER_BITS <= 16, uint16_t,
                  typename std::conditional<INNER_BITS <= 32, uint32_t,
                  typename std::conditional<INNER_BITS <= 64, uint64_t, void>
                  ::type>::type>::type>::type;

  struct Entry {
    alignas(value_type) char as_data[sizeof(value_type)];
    Id as_id;
  };

  Entry* entries_;
  size_type first_free_{INVALID_INDEX};
};


template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
void BasicStableIdVector<ElemType, IdType, OUTER_BITS>::reserve(
    size_type new_capacity) {
  DCHECK_LE(new_capacity, MAX_SIZE);
  const auto old_capacity = entries_.size();
  if (new_capacity > old_capacity) {
    entries_.resize(new_capacity);
    for (auto i = old_capacity; i < new_capacity - 1; ++i) {
      entries_[i].id = static_cast<Id>(i + 1);
    }
    entries_[new_capacity - 1].id = first_free_;
    first_free_ = old_capacity;
    if (last_free_ == INVALID_INDEX) last_free_ = new_capacity - 1;
  }
}

template <typename ElemType, typename IdType, uint8_t OUTER_BITS>
bool BasicStableIdVector<ElemType, IdType, OUTER_BITS>::is_valid_id(
    Id id) const {
  auto outer_id = id & OUTER_MASK;
  return outer_id < entries_.size() && (id == entries_[outer_id].id);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
template<typename... Args>
typename BasicStableIdVector<ElemType, IdType, OUTER_BITS>::Id
    BasicStableIdVector<ElemType, IdType, OUTER_BITS>::emplace(Args&&... args) {
  Id outer_id;
  if (last_free_ == INVALID_INDEX) {
    DCHECK_EQ(first_free_, INVALID_INDEX);
    outer_id = entries_.size();
    entries_.emplace_back();
    DCHECK_LE(outer_id, entries_.size());
  } else {
    DCHECK_NE(first_free_, INVALID_INDEX);
    outer_id = first_free_;
    DCHECK_LE(outer_id, entries_.size());
    first_free_ = entries_[first_free_].id & OUTER_MASK;
    if (first_free_ == INVALID_INDEX) last_free_ = INVALID_INDEX;
  }

  new (reinterpret_cast<value_type*>(entries_[outer_id].data)) value_type(
      std::forward<Args>(args)...);
  ++size_;

  return entries_[outer_id].id =
             (entries_[outer_id].id & INNER_MASK) | outer_id;
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
ElemType& BasicStableIdVector<ElemType, IdType, OUTER_BITS>::operator[](Id id) {
  return const_cast<value_type&>(
      const_cast<const BasicStableIdVector<ElemType, IdType, OUTER_BITS> &>(
          *this)[id]);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
const ElemType& BasicStableIdVector<ElemType, IdType, OUTER_BITS>::operator[](
    Id id) const {
  auto outer_id = id & OUTER_MASK;
  DCHECK_LT(outer_id, entries_.size()) << "Index out of bounds.";

  auto& entry = entries_[outer_id];
  CHECK_EQ(entry.id, id)
      << "Stale index used outers " << (entry.id & OUTER_MASK) << " vs. "
      << outer_id << ", inners: " << ((entry.id & INNER_MASK) >> OUTER_BITS)
      << " vs. " << ((id & INNER_MASK) >> OUTER_BITS);

  return *reinterpret_cast<const value_type*>(entry.data);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
void BasicStableIdVector<ElemType, IdType, OUTER_BITS>::erase(Id freed_id) {
  const Id outer_freed_id = freed_id & OUTER_MASK;
  DCHECK_LE(outer_freed_id, entries_.size());

  Entry& freed_entry = entries_[outer_freed_id];
  DCHECK_EQ(freed_id, freed_entry.id);

  // Destroy stored object.
  reinterpret_cast<value_type*>(freed_entry.data)->~value_type();

  // Increment inner id;
  freed_entry.id =
      (((freed_entry.id & INNER_MASK) + (1 << OUTER_BITS)) & INNER_MASK) |
      OUTER_MASK;
  if (last_free_ == INVALID_INDEX) {
    // Empty free list, set both pointers to the freed id.
    DCHECK_EQ(first_free_, INVALID_INDEX);
    last_free_ = first_free_ = outer_freed_id;
  } else {
    // Add freed entry to the end of the free list.
    entries_[last_free_].id &= INNER_MASK | outer_freed_id;
    last_free_ = outer_freed_id;
  }

  --size_;
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_SHARED_ID_VECTOR_HPP_
