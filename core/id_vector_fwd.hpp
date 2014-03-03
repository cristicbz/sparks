#ifndef SPARKS_CORE_ID_VECTOR_FWD_HPP_
#define SPARKS_CORE_ID_VECTOR_FWD_HPP_

#include <cstdint>
#include <vector>

namespace sparks {

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
class BasicIdVector {
 public:
  using Id = IdType;
  using size_type = IdType;
  using iterator = typename std::vector<ElemType>::iterator;
  using const_iterator = typename std::vector<ElemType>::const_iterator;
  using value_type = ElemType;
  using reference_type = ElemType&;
  using pointer_type = ElemType*;

  static_assert(OUTER_BITS < sizeof(IdType) * 8,
                "There needs to be at least one inner bit.");

  static const Id OUTER_MASK = (1 << OUTER_BITS) - 1;
  static const Id INNER_MASK = ~OUTER_MASK;
  static const Id MAX_INDEX = OUTER_MASK - 1;
  static const Id INVALID_INDEX = OUTER_MASK;
  static const size_type MAX_SIZE = MAX_INDEX + 1;

  inline BasicIdVector(size_type min_ids, size_type min_elements);

  inline void reserve_ids(size_type new_size);
  inline void reserve_elements(size_type new_size);

  inline bool is_valid_id(Id id) const;
  inline Id id_from_iterator(const_iterator iter) const;
  inline Id id_from_pointer(const ElemType* pointer) const;

  template<typename ...Args>
  inline Id emplace(Args&& ...args);
  inline Id insert(const value_type& value);

  inline void erase(Id id);
  inline value_type& operator[](Id id);
  inline const value_type& operator[](Id id) const;

  bool empty() const { return elements_.empty(); }
  size_type size() const { return static_cast<size_type>(elements_.size()); }
  size_type capacity() const {
    return static_cast<size_type>(elements_.capacity());
  }

  const_iterator cbegin() const { return elements_.begin(); }
  const_iterator cend() const { return elements_.end(); }

  const_iterator begin() const { return elements_.begin(); }
  const_iterator end() const { return elements_.end(); }

  iterator begin() { return elements_.begin(); }
  iterator end() { return elements_.end(); }

 private:
  inline Id create_id();
  inline void free_id(Id freed_id);

  std::vector<size_type> outer_to_index_;
  std::vector<size_type> index_to_outer_;
  std::vector<value_type> elements_;

  size_type first_free_ = INVALID_INDEX;
  size_type last_free_ = INVALID_INDEX;
};

template<class ElemType, uint8_t OUTER_BITS = 24>
using IdVector32 = BasicIdVector<ElemType, uint32_t, OUTER_BITS>;

template<class ElemType, uint8_t OUTER_BITS = 56>
using IdVector64 = BasicIdVector<ElemType, uint64_t, OUTER_BITS>;

}  // namespace

#endif  // #ifndef SPARKS_CORE_ID_VECTOR_FWD_HPP_
