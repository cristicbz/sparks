#ifndef SPARKS_CORE_ENUM_TO_STRING_HPP_
#define SPARKS_CORE_ENUM_TO_STRING_HPP_

#include <type_traits>
#include <iostream>

namespace sparks {
template<typename EnumType>
struct EnumNames {
  static const char* names[];
};

template<class EnumType, unsigned SIZE = sizeof(EnumNames<EnumType>::names)>
std::ostream& operator<<(std::ostream& ostream, EnumType enum_value) {
  using int_type = typename std::underlying_type<EnumType>::type;
  static constexpr int_type NUM_NAMES = SIZE / sizeof(const char*);
  const auto enum_int = static_cast<int_type>(enum_value);
  if (enum_int >= 0 && enum_int < NUM_NAMES) {
    return ostream << EnumNames<EnumType>::names[enum_int];
  } else {
    return ostream << "<enum_out_of_range " << enum_int << '>';
  }
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_ENUM_TO_STRING_HPP_
