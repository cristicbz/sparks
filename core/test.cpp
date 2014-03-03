#include <iostream>
#include <thread>
#include <vector>

#include <glog/logging.h>
#include <SDL2/SDL.h>

#include "executor.hpp"

#define SLOG if (false) LOG(INFO)

uint32_t frame_counter = 0;
uint32_t total_frames = 0;
uint64_t last_frame_reset = 0;
double perf_freq = 0;
uint32_t silly_counter = 0;

void new_frame(sparks::Executor& executor) {
  using sparks::Executor;
  using TaskId = sparks::Executor::TaskId;
  uint32_t* ptr = &silly_counter;

  TaskId frame_start = executor.add_task(
      [=] {
        SLOG << "frame_start";
        (*ptr) += 1;
      }, {}, Executor::NO_AFFINITY);

  auto new_time = SDL_GetPerformanceCounter();
  double time_since_reset = (new_time - last_frame_reset) / perf_freq;

  ++frame_counter;
  if (time_since_reset >= 2.0) {
    LOG(INFO) << "Perf " << (time_since_reset * 100.0 / frame_counter)
              << " ms. (" << frame_counter << " frames).";
    last_frame_reset = new_time;
    total_frames += frame_counter;
    frame_counter = 0;

    if (total_frames >= 5000000) executor.close();
  }

  TaskId scene = executor.add_task(
      [=] {
        SLOG << "scene";
        for (int i = 0; i < 1000; ++i) *ptr += i * (*ptr);
      }, {frame_start}, Executor::NO_AFFINITY);

  TaskId anim = executor.add_task(
      [=] {
        SLOG << "anim";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {scene}, Executor::NO_AFFINITY);

  TaskId ai = executor.add_task(
      [=] {
        SLOG << "ai";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {frame_start}, Executor::NO_AFFINITY);

  TaskId ctrl = executor.add_task(
      [=] {
        SLOG << "ctrl";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {frame_start});

  TaskId gameplay = executor.add_task(
      [=] {
        SLOG << "game";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {scene, anim, ai, ctrl});

  TaskId audio = executor.add_task(
      [=] {
        SLOG << "audio";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {gameplay});

  TaskId gui = executor.add_task(
      [=] {
        SLOG << "gui";
        for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
      }, {frame_start}, Executor::NO_AFFINITY);

  TaskId render_start = executor.add_task(
      [=] { ++silly_counter; },
      {scene, anim, gui, gameplay}, Executor::NO_AFFINITY);

  TaskId render_end = executor.add_task(
      [=] { (*ptr) += 1; },
      {
        executor.add_task([=] {
          SLOG << "render1";
          for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
        }, {render_start}),

        executor.add_task([=] {
          SLOG << "render2";
          for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
        }, {render_start}),

        executor.add_task([=] {
          SLOG << "render3";
          for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
        }, {render_start}),

        executor.add_task([=] {
          SLOG << "render4";
          for (int i = 0; i < 1000; ++i) (*ptr) += i * (*ptr);
        }, {render_start}),
      });

  executor.add_task([&]{
      SLOG << "frame_end";
      ++silly_counter;
      new_frame(executor); },
      {frame_start, scene, anim, ai, ctrl, gameplay, audio, gui,
       render_start, render_end});
}

int main() {
  google::InitGoogleLogging("sparks");
  google::LogToStderr();
  perf_freq = static_cast<double>(SDL_GetPerformanceFrequency());

  sparks::Executor executor;
  last_frame_reset = SDL_GetPerformanceCounter();
  //new_frame(executor);
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&executor, i] { executor.run_tasks_no_affinity(); });
  }
  executor.run_tasks_no_affinity();

  LOG(INFO) << silly_counter << " done.";

  for (auto& t : threads) t.join();



  return 0;
}
