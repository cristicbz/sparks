#ifndef SPARKS_CORE_ID_VECTOR_HPP_
#define SPARKS_CORE_ID_VECTOR_HPP_

#include <atomic>
#include <cstddef>
#include <utility>
#include <random>

#include <glog/logging.h>

namespace sparks {

template<typename Element_, typename IntId_, size_t INDEX_BITS>
class BasicIdVector {
 public:
  static_assert(INDEX_BITS < sizeof(IntId_) * 8, "too many index bits");
  static_assert(INDEX_BITS > 0, "zero index bits");

  using Element = Element_;
  using IntId = IntId_;

  enum class Id : IntId_ {};

 private:
  static constexpr IntId INVALID {(1 << INDEX_BITS) - 1};

 public:
  static constexpr Id INVALID_ID {static_cast<Id>(INVALID)};
  static constexpr IntId MAX_INDEX {INVALID - 1};
  static constexpr IntId CAPACITY {MAX_INDEX + 1};

  BasicIdVector() {
    slots_ = new Slot[CAPACITY];
    init_empty();
  }

  // Destruction must be synchronized: all method calls in all threads need to
  // have finished before calling destructor.
  ~BasicIdVector() {
    destroy_elements();
    delete[] slots_;
  }

  // Non-copyable nor movable since it cannot be done lockfree.
  BasicIdVector(const BasicIdVector&) = delete;
  BasicIdVector(BasicIdVector&&) = delete;

  BasicIdVector& operator=(const BasicIdVector&) = delete;
  BasicIdVector& operator=(BasicIdVector&&) = delete;


  // Clearing must be synchronized: all method calls in all threads need to
  // have finished before calling clear().
  void unsafe_clear() {
    destroy_elements();
    init_empty();
  }

  bool is_valid_id(Id id) const {
    auto index = unpack_index(static_cast<IntId>(id));
    if (index >= CAPACITY) return false;
    return slots_[index].id.load() ==
           static_cast<IntId>(id);
  }

  template<typename ...Args>
  std::pair<Id, Element*> emplace(Args&& ...args) {
    if (auto* slot = acquire_slot()) {
      auto* new_element = reinterpret_cast<Element*>(slot->payload);
      new (new_element) Element(std::forward<Args>(args)...);
      return {static_cast<Id>(
                  slot->id.load(std::memory_order::memory_order_acquire)),
              new_element};
    } else {
      return {INVALID_ID, nullptr};
    }
  }

  template<typename ...Args>
  std::pair<Id, Element*> spin_emplace(Args&& ...args) {
    Slot* slot;
    while ((slot = acquire_slot()) == nullptr) {}

    auto* new_element = reinterpret_cast<Element*>(slot->payload);
    new (new_element) Element(std::forward<Args>(args)...);
    return {static_cast<Id>(slot->id.load()),
            new_element};
  }

  const Element& operator[](Id id) const {
    DCHECK(is_valid_id(id)) << static_cast<int>(id);
    return element_at(unpack_index(static_cast<IntId>(id)));
  }

  Element& operator[](Id id) {
    auto index = unpack_index(static_cast<IntId>(id));
    DCHECK(is_valid_id(id))
        << static_cast<int>(id) << " "
        << (index < CAPACITY ? slots_[index].id.load() : 12345678);
    return element_at(unpack_index(static_cast<IntId>(id)));
  }

  bool move_from(Id id, Element& to) {
    if (auto* slot = lock_slot(static_cast<IntId>(id))) {
      auto* element = reinterpret_cast<Element*>(slot->payload);
      to = std::move(*element);
      element->~Element();
      release_locked_slot(slot);
      return true;
    }
    return false;
  }

  // Idempotent.
  void erase(Id id) {
    if (auto* slot = lock_slot(static_cast<IntId>(id))) {
      reinterpret_cast<Element*>(slot->payload)->~Element();
      release_locked_slot(slot);
    }
  }

 private:
  using AtomicId = std::atomic<IntId>;

  struct Slot {
    alignas(Element) char payload[sizeof(Element)];
    AtomicId id;
  };


  static constexpr IntId INDEX_MASK = (1 << INDEX_BITS) - 1;
  static constexpr IntId TAG_MASK = ~INDEX_MASK;
  static constexpr IntId TAG_INCREMENTOR = 1 << INDEX_BITS;


  static IntId unpack_index(IntId id) { return id & INDEX_MASK; }

  static IntId reset_index(IntId id, IntId new_index) {
    DCHECK_LE(new_index, INDEX_MASK);
    return (id & TAG_MASK) | new_index;
  }

  static IntId increment_tag_and_reset(IntId id, IntId new_index) {
    DCHECK_LE(new_index, INDEX_MASK);
    return (((id & TAG_MASK) + TAG_INCREMENTOR) & TAG_MASK) | new_index;
  }

  static IntId increment_tag(IntId id) {
    return increment_tag_and_reset(id, unpack_index(id));
  }

  Slot* lock_slot(IntId id) {
    DCHECK_LT(unpack_index(id), INVALID);
    auto& slot = slots_[unpack_index(id)];
    const auto invalidated = increment_tag(id);
    if (slot.id.compare_exchange_strong(id, invalidated)) {
      DCHECK(is_acquired(unpack_index(id)));
      return &slot;
    }
    return nullptr;  // Lost the race, another call invalidated the slot first.
  }

  bool is_acquired(IntId index) const {
    DCHECK_LT(index, CAPACITY);
    return unpack_index(slots_[index].id.load()) ==
           index;
  }

  Slot& mark_acquired(IntId index) const {
    DCHECK_LT(index, CAPACITY);
    auto& slot = slots_[index];
    slot.id.store(reset_index(slot.id.load(), index));
    return slot;
  }

  Element& element_at(IntId index) const {
    DCHECK(is_acquired(index));
    return *reinterpret_cast<Element*>(slots_[index].payload);
  }

  Slot* acquire_slot() {
    IntId head_mirror, head_index, new_head;
    do {
      head_mirror = free_head_.load();
      head_index = unpack_index(head_mirror);
      if (head_index == INVALID) return nullptr;  // No empty slots.

      auto new_head_index = unpack_index(slots_[head_index].id);
      new_head = increment_tag_and_reset(head_mirror, new_head_index);
    } while (!free_head_.compare_exchange_weak(head_mirror, new_head));

    return &mark_acquired(head_index);
  }

  void release_locked_slot(Slot* locked) {
    IntId released_id = locked->id.load();
    IntId released_index = unpack_index(released_id);
    IntId head_mirror, new_head;
    do {
      head_mirror = free_head_.load();
      new_head = increment_tag_and_reset(head_mirror, released_index);

      locked->id.store(reset_index(released_id, unpack_index(head_mirror)));
    } while (!free_head_.compare_exchange_weak(head_mirror, new_head));

  }

  void destroy_elements() {
    for (IntId i_slot = 0; i_slot < CAPACITY; ++i_slot) {
      if (is_acquired(i_slot)) element_at(i_slot).~Element();
    }
  }

  void init_empty() {
    // We generate initial tags randomly to make mixups of IDs from different
    // IdVector-s less likely.
    std::linear_congruential_engine<uint64_t, 2862933555777941757uL,
                                    3037000493uL, static_cast<uint64_t>(-1)>
        tag_generator(reinterpret_cast<uint64_t>(this));
    auto tag = [&] { return static_cast<IntId>(tag_generator()) & TAG_MASK; };

    free_head_.store(0);
    for (IntId i_slot = 0; i_slot < CAPACITY - 1; ++i_slot) {
      slots_[i_slot].id.store(i_slot + 1 | tag());
    }
    slots_[CAPACITY - 1].id.store(INVALID | tag());
  }

  Slot* slots_;
  AtomicId free_head_ {INVALID};
};

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::INVALID;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::Id
    BasicIdVector<E, I, IB>::INVALID_ID;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::MAX_INDEX;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::CAPACITY;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::INDEX_MASK;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::TAG_MASK;

template <typename E, typename I, size_t IB>
constexpr typename BasicIdVector<E, I, IB>::IntId
    BasicIdVector<E, I, IB>::TAG_INCREMENTOR;

}  // namespace sparks

#endif  // #ifndef SPARKS_CORE_ID_VECTOR_HPP_

