#ifndef SPARKS_SPECIAL_THREADS_HPP_
#define SPARKS_SPECIAL_THREADS_HPP_

#include <cstdint>

#include "enum_to_string.hpp"

namespace sparks {
enum class SpecialThreadId : uint16_t {
  MAIN_THREAD,                  // int main(), SDL_Init() etc.
  RENDER_THREAD = MAIN_THREAD,  // SDL kind of requires this.
  INPUT_THREAD = MAIN_THREAD,   // Again, required by SDL.
  DISK_IO_THREAD,               // Shares a GL context to allow async textures.
};

template<> const char* EnumNames<SpecialThreadId>::names[] = {
  "MAIN_THREAD", "DISK_IO_THREAD" };

}  // namespace sparks

#endif  // #ifndef SPARKS_SPECIAL_THREADS_HPP_
