#include "id_vector.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <random>

#include <gtest/gtest.h>

namespace sparks {

namespace {
struct Element {
  Element()
      : x{global_count.fetch_add(1)}, creator{std::this_thread::get_id()} {}

  explicit Element(int x) : x{x}, creator{std::this_thread::get_id()} {
    global_count.fetch_add(1);
  }

  Element(int a, int b) : x{a + b}, creator{std::this_thread::get_id()} {
    global_count.fetch_add(1);
  }

  ~Element() {
    x = 123456789;
    EXPECT_TRUE(suppress_diff_destroyer ||
                creator == std::this_thread::get_id()) << "different destroyer";
    global_count.fetch_sub(1); }

  Element(const Element&) = delete;
  Element(Element&&) = delete;

  Element& operator=(const Element&) = delete;
  Element& operator=(Element&&) = delete;

  int value() const { return x; }

  static int count() { return global_count.load(); }

  void expect(int expected_x) const { EXPECT_EQ(expected_x, x); }

  bool am_creator() const { return creator == std::this_thread::get_id(); }

  static std::atomic<int> global_count;
  static std::atomic<bool> suppress_diff_destroyer;

 private:
  int x;
  std::thread::id creator;
};

std::atomic<int> Element::global_count{0};
std::atomic<bool> Element::suppress_diff_destroyer{false};

struct IdVectorTest : public ::testing::Test {
  using SmallVector = BasicIdVector<Element, uint8_t, 3>;
  using BigVector = BasicIdVector<Element, uint32_t, 12>;

  static_assert(SmallVector::CAPACITY == 7, "");
  static_assert(BigVector::CAPACITY == 4095, "");

  std::unique_ptr<SmallVector> small_ptr{new SmallVector{}};
  std::unique_ptr<BigVector> big_ptr{new BigVector{}};

  SmallVector& small;
  BigVector& big;

  IdVectorTest() : small{*small_ptr}, big{*big_ptr} {}

  void TearDown() {
    small_ptr.reset();
    big_ptr.reset();
    ASSERT_EQ(0, Element::count());
  }
};

TEST_F(IdVectorTest, SingleThreadedAutoDestruction) {
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(small.emplace().second != nullptr);
  }

  // TearDown() checks that elems are destructed correctly.
}

TEST_F(IdVectorTest, SingleThreadedAddAndRemove) {
  SmallVector::Id ids[7];
  for (int i = 0; i < 7; ++i) {
    auto emplacement = small.emplace();
    ASSERT_TRUE(emplacement.second != nullptr);
    EXPECT_EQ(i, emplacement.second->value());
    EXPECT_TRUE(small.is_valid_id(emplacement.first));
    EXPECT_TRUE(&small[emplacement.first] == emplacement.second);

    ids[i] = emplacement.first;
  }

  EXPECT_TRUE(small.emplace().second == nullptr);
  small.erase(ids[3]);
  EXPECT_FALSE(small.is_valid_id(ids[3]));
  ids[3] = small.emplace(1, 2).first;
  EXPECT_EQ(7, Element::count());

  for (int j = 0; j < 8; ++j) {
    for (int i = 0; i < 7; ++i) {
      if (i < j) {
        EXPECT_FALSE(small.is_valid_id(ids[i]));
      } else {
        EXPECT_TRUE(small.is_valid_id(ids[i]));
        EXPECT_EQ(i, small[ids[i]].value());
      }
    }
    if (j < 7) small.erase(ids[j]);
  }
}

TEST_F(IdVectorTest, LongManyThreadsAddAndRemoveTakes15) {
  constexpr size_t MAX_IDS = 4095;
  constexpr size_t MAX_ITER = 8192;
  constexpr size_t NUM_THREADS[] = { 1, 2, 3, 4, 6, 9, 13, 24, 32 };
  constexpr size_t NUM_NUM_THREADS =
      sizeof(NUM_THREADS) / sizeof(NUM_THREADS[0]);

  for (int i_num_threads = 0; i_num_threads < NUM_NUM_THREADS;
       ++i_num_threads) {
    auto num_threads = NUM_THREADS[i_num_threads];
    std::vector<std::vector<std::pair<BigVector::Id, int>>> all_ids_and_values;
    all_ids_and_values.resize(num_threads);

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    VLOG(1) << "Trying with " << num_threads << " threads. Iterating...";
    Element::suppress_diff_destroyer.store(false);
    for (int i_thread = 0; i_thread < num_threads; ++i_thread) {
      threads.emplace_back([this, &all_ids_and_values, i_thread, num_threads] {
        auto& ids_and_values = all_ids_and_values[i_thread];
        const size_t max_ids =
            (i_thread < num_threads - 1)
                ? (MAX_IDS / num_threads)
                : (MAX_IDS - (MAX_IDS / num_threads) * (num_threads - 1));
        ASSERT_LT(0, max_ids);
        ASSERT_EQ(0, ids_and_values.size());
        ids_and_values.reserve(MAX_IDS);
        std::minstd_rand0 gen{i_thread};
        for (int i_iter = 0; i_iter < MAX_ITER; ++i_iter) {
          for (const auto& id_and_value : ids_and_values) {
            EXPECT_TRUE(big.is_valid_id(id_and_value.first));
            EXPECT_EQ(id_and_value.second, big[id_and_value.first].value())
                << i_iter << ": "
                << static_cast<size_t>(&id_and_value - &ids_and_values[0])
                << "/" << ids_and_values.size()
                << " :: id=" << static_cast<uint64_t>(id_and_value.first);
          }

          if (ids_and_values.size() < max_ids &&
              (ids_and_values.size() == 0 || gen() % 100 <= 60)) {
            int new_value = gen() % 100000;
            auto id = big.spin_emplace(new_value).first;
            ids_and_values.emplace_back(id, new_value);
            EXPECT_NE(id, BigVector::INVALID_ID);
            EXPECT_TRUE(big.is_valid_id(id));
          } else {
            ASSERT_LT(0, ids_and_values.size());
            int m = gen() % ids_and_values.size();
            big.erase(ids_and_values[m].first);
            EXPECT_FALSE(big.is_valid_id(ids_and_values[m].first));
            ids_and_values[m] = ids_and_values.back();
            ids_and_values.pop_back();
          }
        }
      });
    }

    for (auto& thread : threads) thread.join();
    threads.clear();
    Element::suppress_diff_destroyer.store(true);

    size_t expected_size = 0;
    for (int i_thread = 0; i_thread < num_threads; ++i_thread) {
      expected_size += all_ids_and_values[i_thread].size();
    }
    EXPECT_EQ(expected_size, big.size());
    EXPECT_EQ(expected_size, Element::count());

    VLOG(1) << "Parallel remove...";
    for (int i_thread = 0; i_thread < num_threads; ++i_thread) {
      threads.emplace_back([this, &all_ids_and_values, i_thread, num_threads]{
        for (int j_thread = 0; j_thread < num_threads; ++j_thread) {
          for (const auto& id_and_value :
               all_ids_and_values[(j_thread + i_thread) % num_threads]) {
            big.erase(id_and_value.first);
            EXPECT_FALSE(big.is_valid_id(id_and_value.first));
          }
        }
      });
    }

    for (auto& thread : threads) thread.join();
    threads.clear();

    EXPECT_EQ(0, big.size());
    EXPECT_EQ(0, Element::count());

    big.unsafe_clear();
    ASSERT_EQ(0, big.size());
    ASSERT_EQ(0, Element::count());

    VLOG(1) << "Done.";
  }
}

}  // namespace
}  // namespace sparks
