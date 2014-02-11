#ifndef SPARKS_CORE_HASHES_HPP_
#define SPARKS_CORE_HASHES_HPP_

#include <cstring>
#include <cstddef>
#include <cstdint>

namespace sparks {

namespace core {
namespace detail {
constexpr uint64_t murmur64_last(uint64_t h) {
    return ((h ^ (h >> 47)) * 0xc6a4a7935bd1e995) ^
        (((h ^ (h >> 47)) * 0xc6a4a7935bd1e995) >> 47);
}

constexpr uint64_t murmur64_tail_1(uint64_t h, const unsigned char* data2) {
    return murmur64_last((h ^ uint64_t(data2[0])) * 0xc6a4a7935bd1e995);
}
constexpr uint64_t murmur64_tail_2(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_1((h ^ (uint64_t(data2[1]) << 8)) * 0xc6a4a7935bd1e995,
                            data2);
}
constexpr uint64_t murmur64_tail_3(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_2((h ^ (uint64_t(data2[2]) << 16)) *
                           0xc6a4a7935bd1e995, data2);
}
constexpr uint64_t murmur64_tail_4(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_3((h ^ (uint64_t(data2[3]) << 24)) *
                           0xc6a4a7935bd1e995, data2);
}
constexpr uint64_t murmur64_tail_5(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_4((h ^ (uint64_t(data2[4]) << 32)) *
                           0xc6a4a7935bd1e995, data2);
}
constexpr uint64_t murmur64_tail_6(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_5((h ^ (uint64_t(data2[5]) << 40)) *
                           0xc6a4a7935bd1e995, data2);
}
constexpr uint64_t murmur64_tail_7(uint64_t h, const unsigned char* data2) {
    return murmur64_tail_6((h ^ (uint64_t(data2[6]) << 48)) *
                           0xc6a4a7935bd1e995, data2);
}
constexpr uint64_t murmur64_rest(uint64_t h, size_t len,
                                 const unsigned char *data2) {
    return ((len & 7) == 7) ? murmur64_tail_7(h, data2) :
           ((len & 7) == 6) ? murmur64_tail_6(h, data2) :
           ((len & 7) == 5) ? murmur64_tail_5(h, data2) :
           ((len & 7) == 4) ? murmur64_tail_4(h, data2) :
           ((len & 7) == 3) ? murmur64_tail_3(h, data2) :
           ((len & 7) == 2) ? murmur64_tail_2(h, data2) :
           ((len & 7) == 1) ? murmur64_tail_1(h, data2) :
                            murmur64_last(h);
}

constexpr uint64_t load8(const char* data) {
    return (uint64_t(data[7]) << 56) | (uint64_t(data[6]) << 48) |
           (uint64_t(data[5]) << 40) | (uint64_t(data[4]) << 32) |
           (uint64_t(data[3]) << 24) | (uint64_t(data[2]) << 16) |
           (uint64_t(data[1]) <<  8) | (uint64_t(data[0]) <<  0);
}

constexpr uint64_t murmur64_loop(size_t i, uint64_t h, size_t len,
                                 const char *data) {
    return (i == 0 ?
            murmur64_rest(h, len, (const unsigned char*) data) :
            murmur64_loop(i-1,
                          (h ^ (((load8(data) * 0xc6a4a7935bd1e995) ^
                                 ((load8(data) * 0xc6a4a7935bd1e995) >> 47)) *
                                0xc6a4a7935bd1e995)) * 0xc6a4a7935bd1e995,
                          len, data+8));
}

// NOTE: Guaranteed to be random by fair dice roll.
constexpr uint32_t default_murmur_seed = 0x144cbe2f;
}  // namespace detail
}  // namespace core

constexpr uint64_t murmur64_with_seed(const char *key, size_t length,
                                      uint32_t seed) {
  return core::detail::murmur64_loop(length / 8, seed ^ (length * 47), length,
                                     key);
}

inline uint64_t murmur64(const char* key, size_t maximum_length) {
  return murmur64_with_seed(key, strnlen(key, maximum_length),
                            core::detail::default_murmur_seed);
}

template<size_t kKeySize>
constexpr uint64_t static_murmur64(const char (&key)[kKeySize]) {
  return murmur64_with_seed(key, kKeySize, core::detail::default_murmur_seed);
}

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_HASHES_HPP_
