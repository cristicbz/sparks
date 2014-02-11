#ifndef SPARKS_CORE_WINDOW_HPP_
#define SPARKS_CORE_WINDOW_HPP_

#include <cstdint>

struct SDL_Window;

namespace sparks {
class Application;
class Window {
 public:
  enum class Fullscreen { NO, YES };

  Window(const Application& app);
  ~Window();

  bool open(const char* title, uint32_t pixel_width, uint32_t pixel_height,
            Fullscreen fullscreen = Fullscreen::NO);
  bool is_opened() const { return window_ == nullptr; }

  void toggle_fullscreen();

 private:
  SDL_Window* window_ = nullptr;
};

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_WINDOW_HPP_

