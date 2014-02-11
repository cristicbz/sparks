#ifndef SPARKS_APPLICATION_HPP_
#define SPARKS_APPLICATION_HPP_

namespace sparks {

class Application {
public:
  Application(const char* argv0);
  ~Application();

  static bool on_main_thread();
};

}  // namespace sparks

#endif  // #ifndef SPARKS_APPLICATION_HPP_
