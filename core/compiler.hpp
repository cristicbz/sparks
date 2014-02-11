#ifndef SPARKS_CORE_COMPILER_HPP_
#define SPARKS_CORE_COMPILER_HPP_

#if defined(_MSC_VER)
#define SPARKS_FORCE_INLINE __forceinline
#else  // #if defined(_MSC_VER)
#define SPARKS_FORCE_INLINE inline __attribute_((always_inline))
#endif

namespace sparks {

}

#endif  //#ifndef SPARKS_CORE_COMPILER_HPP_
