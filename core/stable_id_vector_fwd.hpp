#ifndef SPARKS_CORE_STABLE_ID_VECTOR_FWD_HPP_
#define SPARKS_CORE_STABLE_ID_VECTOR_FWD_HPP_

#include <cstdint>
#include <type_traits>
#include <vector>

namespace sparks {

template<typename ElemType, typename IdType, uint8_t OUTER_BITS>
class BasicStableIdVector {
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

  inline BasicStableIdVector(size_type capacity);
  inline ~BasicStableIdVector();

  inline void reserve(size_type new_capacity);

  inline bool is_valid_id(Id id) const;

  template<typename ...Args>
  inline Id emplace(Args&& ...args);

  inline void erase(Id id);
  inline value_type& operator[](Id id);
  inline const value_type& operator[](Id id) const;

  size_type capacity() const {
    return static_cast<size_type>(entries_.size());
  }

 private:
  using InnerId = typename std::conditional<INNER_BITS <=  8,  uint8_t,
                  typename std::conditional<INNER_BITS <= 16, uint16_t,
                  typename std::conditional<INNER_BITS <= 32, uint32_t,
                  typename std::conditional<INNER_BITS <= 64, uint64_t, void>
                  ::type>::type>::type>::type;

  inline Id create_id();
  inline void free_id(Id freed_id);

  struct Entry {
    alignas(value_type) char data[sizeof(value_type)];
    Id id;
  };

  std::vector<Entry> entries_;

  size_type first_free_ = INVALID_INDEX;
  size_type last_free_ = INVALID_INDEX;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_STABLE_ID_VECTOR_FWD_HPP_
