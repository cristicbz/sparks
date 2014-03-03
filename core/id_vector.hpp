#ifndef SPARKS_CORE_ID_VECTOR_HPP_
#define SPARKS_CORE_ID_VECTOR_HPP_

#include "id_vector_fwd.hpp"

#include <glog/logging.h>
#include <iostream>

namespace sparks {

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
BasicIdVector<ElemType, IdType, OUTER_BITS>::BasicIdVector(
    size_type min_ids, size_type min_elements) {
  reserve_ids(min_ids);
  reserve_elements(min_elements);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
void BasicIdVector<ElemType, IdType, OUTER_BITS>::reserve_ids(size_type new_size) {
  DCHECK_LE(new_size, MAX_SIZE);
  const auto old_size = outer_to_index_.size();
  if (new_size > old_size) {
    outer_to_index_.resize(new_size);
    for (auto i = old_size; i < new_size - 1; ++i) {
      outer_to_index_[i] = static_cast<Id>(i + 1);
    }
    outer_to_index_[new_size - 1] = first_free_;
    first_free_ = old_size;
    if (last_free_ == INVALID_INDEX) last_free_ = new_size - 1;
  }

}

template <typename ElemType, typename IdType, uint8_t OUTER_BITS>
void BasicIdVector<ElemType, IdType, OUTER_BITS>::reserve_elements(
    size_type new_size) {
  DCHECK_LE(new_size, MAX_SIZE);
  elements_.reserve(new_size);
  reserve_ids(new_size);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
bool BasicIdVector<ElemType, IdType, OUTER_BITS>::is_valid_id(Id id) const {
  auto outer_id = id & OUTER_MASK;
  return outer_id < outer_to_index_.size() &&
         (id & INNER_MASK) == (outer_to_index_[outer_id] & INNER_MASK);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
typename BasicIdVector<ElemType, IdType, OUTER_BITS>::Id BasicIdVector<
    ElemType, IdType, OUTER_BITS>::id_from_iterator(const_iterator iter) const {
  DCHECK(iter <= elements_.end());
  DCHECK(iter >= elements_.begin());
  if (iter == elements_.end()) return INVALID_INDEX;
  const auto outer_id = index_to_outer_[iter - elements_.begin()];
  return (outer_to_index_[outer_id] & INNER_MASK) | outer_id;
}

template <typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
typename BasicIdVector<ElemType, IdType, OUTER_BITS>::Id
BasicIdVector<ElemType, IdType, OUTER_BITS>::id_from_pointer(
    const ElemType* pointer) const {
  DCHECK(!elements_.empty() && ((pointer - &elements_[0]) <= elements_.size()));
  return id_from_iterator(elements_.begin() + (pointer - &elements_.front()));
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
template<typename... Args> inline
typename BasicIdVector<ElemType, IdType, OUTER_BITS>::Id
    BasicIdVector<ElemType, IdType, OUTER_BITS>::emplace(Args&&... args) {
  const auto id = create_id();
  elements_.emplace_back(std::forward<Args>(args)...);
  index_to_outer_.emplace_back(id & OUTER_MASK);
  return id;
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
typename BasicIdVector<ElemType, IdType, OUTER_BITS>::Id inline
    BasicIdVector<ElemType, IdType, OUTER_BITS>::insert(
        const value_type &value) {
  const auto id = create_id();
  elements_.push_back(value);
  index_to_outer_.emplace_back(id & OUTER_MASK);
  return id;
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
void BasicIdVector<ElemType, IdType, OUTER_BITS>::erase(Id id) {
  free_id(id);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
ElemType& BasicIdVector<ElemType, IdType, OUTER_BITS>::operator[](Id id) {
  return const_cast<value_type&>(
      const_cast<const BasicIdVector<ElemType, IdType, OUTER_BITS> &>(
          *this)[id]);
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS> inline
const ElemType& BasicIdVector<ElemType, IdType, OUTER_BITS>::operator[](
    Id id) const {
  auto outer_id = id & OUTER_MASK;
  DCHECK_LT(outer_id, outer_to_index_.size()) << "Index out of bounds.";

  auto& entry = outer_to_index_[outer_id];
  DCHECK_EQ(entry & INNER_MASK, id & INNER_MASK) << "Stale index used.";

  auto element_index = entry & OUTER_MASK;
  DCHECK_LT(element_index, elements_.size());
  DCHECK_EQ(index_to_outer_[element_index], outer_id);

  return elements_[element_index];
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
typename BasicIdVector<ElemType, IdType, OUTER_BITS>::Id
    BasicIdVector<ElemType, IdType, OUTER_BITS>::create_id() {
  const auto element_index = static_cast<Id>(elements_.size());
  DCHECK_LT(element_index, MAX_INDEX);
  if (last_free_ == INVALID_INDEX) {
    DCHECK_EQ(first_free_, INVALID_INDEX);
    outer_to_index_.push_back(element_index | 0);
    return outer_to_index_.size() - 1;
  } else {
    DCHECK_NE(first_free_, INVALID_INDEX);
    auto& free_index = outer_to_index_[first_free_];
    const auto inner_id = free_index & INNER_MASK;
    const auto outer_id = first_free_;
    first_free_ = free_index & OUTER_MASK;
    if (first_free_ == INVALID_INDEX) last_free_ = INVALID_INDEX;
    free_index = inner_id | element_index;

    return inner_id | outer_id;
  }
}

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
void BasicIdVector<ElemType, IdType, OUTER_BITS>::free_id(Id freed_id) {
  const auto outer_freed_id = freed_id & OUTER_MASK;
  const auto inner_freed_id = freed_id & INNER_MASK;
  DCHECK_LE(outer_freed_id, outer_to_index_.size());

  auto& freed_entry = outer_to_index_[outer_freed_id];
  DCHECK_EQ(inner_freed_id, freed_entry & INNER_MASK);

  const auto element_index = freed_entry & OUTER_MASK;
  // Move last element in place of freed element.
  elements_[element_index] = std::move(elements_.back());
  elements_.pop_back();

  index_to_outer_[element_index] = index_to_outer_.back();
  index_to_outer_.pop_back();

  // Update the outer_to_index_ entry of the last element to point to the new
  // position.
  auto& last_element_entry = outer_to_index_[index_to_outer_[element_index]];
  last_element_entry = (last_element_entry & ~OUTER_MASK) | element_index;

  // Add freed entry at the end of free list and mark it as end-of-list.
  freed_entry =
      (((freed_entry & INNER_MASK) + (1 << OUTER_BITS)) & INNER_MASK) |
      OUTER_MASK;
  if (last_free_ == INVALID_INDEX) {
    DCHECK_EQ(first_free_, INVALID_INDEX);
    last_free_ = first_free_ = outer_freed_id;  // List was empty.
  } else {
    outer_to_index_[last_free_] &= INNER_MASK | outer_freed_id;
    last_free_ = outer_freed_id;
  }
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_ID_VECTOR_HPP_
