#include "stable_id_vector_fwd.hpp"

#include <glog/logging.h>

namespace sparks {

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
BasicStableIdVector<ElemType, IdType, OUTER_BITS>::BasicStableIdVector(
    size_type capacity) {
  reserve(capacity);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
BasicStableIdVector<ElemType, IdType, OUTER_BITS>::~BasicStableIdVector() {
  size_type remaining = size_;
  for (IdType i = 0; i < entries_.size(); ++i) {
    if ((entries_[i].id & OUTER_MASK) == i) {
      LOG(INFO) << this << ": " << entries_[i].id << " " << i;
      reinterpret_cast<value_type*>(entries_[i].data)->~value_type();
      --remaining;
    }
  }
  DCHECK_EQ(remaining, 0);
}

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
