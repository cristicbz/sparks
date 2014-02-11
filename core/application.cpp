#include "application.hpp"

#include <thread>

#include <SDL2/SDL.h>
#include <glog/logging.h>

namespace sparks {

namespace {
std::thread::id g_main_thread_id;
}  // namespace

Application::Application(const char* argv0) {
  google::InitGoogleLogging(argv0);
  google::LogToStderr();
  LOG(INFO) << "Initializing low level video & audio subsystems...";
  CHECK_EQ(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), 0)
    << SDL_GetError();
  g_main_thread_id = std::this_thread::get_id();
  LOG(INFO) << "Done. Main thread id: " << g_main_thread_id;
}

bool Application::on_main_thread() {
  return std::this_thread::get_id() == g_main_thread_id;
}

Application::~Application() {
  LOG(INFO) << "Application destroyed.";
  SDL_Quit();
}

}  // namespace sparks

