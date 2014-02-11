#include "window.hpp"
#include "application.hpp"

#include <SDL2/SDL.h>

#include <glog/logging.h>

namespace sparks {

Window::Window(const Application& /* unused */) {
  CHECK(Application::on_main_thread());
}

bool Window::open(const char* title, uint32_t pixel_width,
                  uint32_t pixel_height, Fullscreen fullscreen) {
  window_ =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       pixel_width, pixel_height, SDL_WINDOW_OPENGL);

  LOG_IF(WARNING, !window_) << "Could not create window " << pixel_width << 'x'
                            << pixel_height << ": " << SDL_GetError();

  return window_ != nullptr;
}

Window::~Window() {
  if (window_) SDL_DestroyWindow(window_);
}

}  // namespace sparks

