#include "work_stealing_queue.hpp"

#include <atomic>
#include <thread>
#include <vector>
#include <random>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace sparks {
namespace {

using Element = uint32_t;
using SmallPool = WorkStealingQueue<Element, 2>;
using LargePool = WorkStealingQueue<Element, 24>;

TEST(WorkStealingQueueTest, SingleThreadedUnique) {
  SmallPool small;
  Element to;

  EXPECT_TRUE(small.empty());
  EXPECT_EQ(0, small.size());
  EXPECT_FALSE(small.unique_pull(to));

  EXPECT_TRUE(small.unique_push(1));
  EXPECT_TRUE(small.unique_push(2));
  EXPECT_TRUE(small.unique_push(3));
  EXPECT_FALSE(small.unique_push(4));
  EXPECT_EQ(3, small.size());

  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(3, to);
  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(2, to);
  EXPECT_EQ(1, small.size());

  EXPECT_TRUE(small.unique_push(4));
  EXPECT_TRUE(small.unique_push(5));
  EXPECT_EQ(3, small.size());

  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(5, to);
  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(4, to);
  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(1, to);
  EXPECT_EQ(0, small.size());
  EXPECT_FALSE(small.unique_pull(to));
  EXPECT_TRUE(small.empty());
}

TEST(WorkStealingQueueTest, SingleThreadedUniqueAndShared) {
  SmallPool small;
  Element to;
  EXPECT_FALSE(small.shared_pull(to));

  EXPECT_TRUE(small.unique_push(1));
  EXPECT_TRUE(small.unique_push(2));
  EXPECT_TRUE(small.unique_push(3));
  EXPECT_EQ(3, small.size());

  EXPECT_TRUE(small.shared_pull(to)); EXPECT_EQ(1, to);
  EXPECT_TRUE(small.shared_pull(to)); EXPECT_EQ(2, to);
  EXPECT_EQ(1, small.size());

  EXPECT_TRUE(small.unique_push(4));
  EXPECT_TRUE(small.unique_push(5));
  EXPECT_EQ(3, small.size());

  EXPECT_TRUE(small.unique_pull(to)); EXPECT_EQ(5, to);
  EXPECT_TRUE(small.shared_pull(to)); EXPECT_EQ(3, to);
  EXPECT_TRUE(small.shared_pull(to)); EXPECT_EQ(4, to);
  EXPECT_EQ(0, small.size());
  EXPECT_TRUE(small.empty());

  EXPECT_FALSE(small.shared_pull(to));
  EXPECT_FALSE(small.unique_pull(to));
  EXPECT_EQ(0, small.size());
  EXPECT_TRUE(small.empty());
}

TEST(WorkStealingQueueTest, ManyThreads) {
  constexpr size_t NUM_ENTRIES = 32;
  constexpr size_t NUM_ITERS = 1 << 18;
  constexpr size_t NUM_THREADS[] = { 2, 3, 4, 6, 9, 13, 24, 32 };
  constexpr size_t NUM_NUM_THREADS =
      sizeof(NUM_THREADS) / sizeof(NUM_THREADS[0]);

  for (int i_num_threads = 0; i_num_threads < NUM_NUM_THREADS;
       ++i_num_threads) {
    LargePool pool;
    auto num_threads = NUM_THREADS[i_num_threads];
    VLOG(1) << "With " << num_threads << " threads.";
    std::vector<std::thread> threads;
    std::vector<std::atomic<uint32_t>> produced;
    std::vector<std::atomic<uint32_t>> consumed;
    std::vector<std::atomic<uint32_t>> consumed_by;
    std::atomic<bool> last_thread_ready{false};
    std::atomic<bool> closed{false};
    threads.reserve(num_threads);
    produced.reserve(NUM_ENTRIES);
    consumed.reserve(NUM_ENTRIES);
    consumed_by.reserve(num_threads);
    for (size_t i_entry = 0; i_entry < NUM_ENTRIES; ++i_entry) {
      produced.emplace_back(0);
      consumed.emplace_back(0);
    }

    for (int i_thread = 0; i_thread < num_threads; ++i_thread) {
      consumed_by.emplace_back(0);
      threads.emplace_back([&pool, &consumed, &closed, &last_thread_ready,
                            &consumed_by, i_thread, num_threads] {
        std::minstd_rand0 gen{i_thread};
        if (i_thread == num_threads - 1) last_thread_ready.store(true);
        while (!closed.load()) {
          Element element{0};
          if (pool.shared_pull(element, std::chrono::milliseconds{100})) {
            consumed[element].fetch_add(1);
            consumed_by[i_thread].fetch_add(1);
          }
          if (gen() % 100 < 10) std::this_thread::yield();
        }
      });
    }

    while (!last_thread_ready.load()) std::this_thread::yield();

    std::minstd_rand0 gen{42};
    for (size_t i_iter = 0; i_iter < NUM_ITERS; ++i_iter) {
      Element element;
      if (gen() % 100 <= 80) {  // 80% of the time add things.
        element = gen() % NUM_ENTRIES;
        if (pool.unique_push(element)) {
          produced[element].fetch_add(1);
        } else {
          --i_iter;  // Redo iteration pool full.
        }
      } else if (pool.unique_pull(element)) {
        consumed[element].fetch_add(1);
      } else {
        --i_iter;  // Redo iteration if pool empty.
      }
    }

    // Pull remaining elements.
    Element element;
    while (pool.unique_pull(element)) consumed[element].fetch_add(1);

    closed.store(true);
    for (auto& thread : threads) thread.join();

    for (size_t i_entry = 0; i_entry < NUM_ENTRIES; ++i_entry) {
      EXPECT_EQ(produced[i_entry], consumed[i_entry]);
    }

    // Show how many elements were consumed by each thread.
    std::stringstream ss;
    for (int i_thread = 0; i_thread < num_threads; ++i_thread) {
      ss << i_thread << ":" << consumed_by[i_thread] << " ";
    }

    VLOG(1) << "Consumed by thread:\n" << ss.str();
  }

}

}  // namespace
}  // namespace sparks
