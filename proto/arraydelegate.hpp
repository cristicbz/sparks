#pragma once
#ifndef ARRAYDELEGATE_HPP
# define ARRAYDELEGATE_HPP

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <functional>

namespace sparks
{

template <typename T> class arraydelegate;

template<class R, class ...A>
class arraydelegate<R (A...)>
{
  static constexpr auto max_store_size = 3 * sizeof(::std::size_t);

  using stub_ptr_type = R (*)(void*, A&&...);

  arraydelegate(void* const o, stub_ptr_type const m) noexcept :
    object_ptr_(o),
    stub_ptr_(m)
  {
  }

public:
  arraydelegate() = default;

  arraydelegate(arraydelegate&& rhs) { *this = rhs; }

  arraydelegate(::std::nullptr_t const) noexcept : arraydelegate() { }

  template <class C, typename =
    typename ::std::enable_if< ::std::is_class<C>{}>::type>
  explicit arraydelegate(C const* const o) noexcept :
    object_ptr_(const_cast<C*>(o))
  {
  }

  template <class C>
  arraydelegate(C* const object_ptr, R (C::* const method_ptr)(A...))
  {
    *this = std::move(from(object_ptr, method_ptr));
  }

  template <class C>
  arraydelegate(C* const object_ptr, R (C::* const method_ptr)(A...) const)
  {
    *this = from(object_ptr, method_ptr);
  }

  template <class C>
  arraydelegate(C& object, R (C::* const method_ptr)(A...))
  {
    *this = from(object, method_ptr);
  }

  template <class C>
  arraydelegate(C const& object, R (C::* const method_ptr)(A...) const)
  {
    *this = from(object, method_ptr);
  }

  template <
    typename T,
    typename = typename ::std::enable_if<
      !::std::is_same<arraydelegate, typename ::std::decay<T>::type>{}
    >::type
  >
  __attribute__((always_inline))
  arraydelegate(T&& f)
  {
    using functor_type = typename ::std::decay<T>::type;

    static_assert(sizeof(functor_type) <= sizeof(store_),
      "increase store_ size");
    new (store_) functor_type(::std::forward<T>(f));

    object_ptr_ = store_;
    stub_ptr_ = functor_stub<functor_type>;

    deleter_ = deleter_stub<functor_type>;
  }

  ~arraydelegate() { deleter_(this); }

  template <class C>
  arraydelegate& operator=(R (C::* const rhs)(A...))
  {
    return *this = from(static_cast<C*>(object_ptr_), rhs);
  }

  template <class C>
  arraydelegate& operator=(R (C::* const rhs)(A...) const)
  {
    return *this = from(static_cast<C const*>(object_ptr_), rhs);
  }

  template <
    typename T,
    typename = typename ::std::enable_if<
      !::std::is_same<arraydelegate, typename ::std::decay<T>::type>{}
    >::type
  >
  arraydelegate& operator=(T&& f)
  {
    using functor_type = typename ::std::decay<T>::type;

    deleter_(store_);

    static_assert(sizeof(functor_type) <= sizeof(store_),
      "increase store_ size");
    new (store_) functor_type(::std::forward<T>(f));

    object_ptr_ = store_;
    stub_ptr_ = functor_stub<functor_type>;

    deleter_ = deleter_stub<functor_type>;

    return *this;
  }

  template <R (* const function_ptr)(A...)>
  static arraydelegate from() noexcept
  {
    return { nullptr, function_stub<function_ptr> };
  }

  template <class C, R (C::* const method_ptr)(A...)>
  static arraydelegate from(C* const object_ptr) noexcept
  {
    return { object_ptr, method_stub<C, method_ptr> };
  }

  template <class C, R (C::* const method_ptr)(A...) const>
  static arraydelegate from(C const* const object_ptr) noexcept
  {
    return { const_cast<C*>(object_ptr), const_method_stub<C, method_ptr> };
  }

  template <class C, R (C::* const method_ptr)(A...)>
  static arraydelegate from(C& object) noexcept
  {
    return { &object, method_stub<C, method_ptr> };
  }

  template <class C, R (C::* const method_ptr)(A...) const>
  static arraydelegate from(C const& object) noexcept
  {
    return { const_cast<C*>(&object), const_method_stub<C, method_ptr> };
  }

  template <typename T>
  static arraydelegate from(T&& f)
  {
    return ::std::forward<T>(f);
  }

  static arraydelegate from(R (* const function_ptr)(A...))
  {
    return function_ptr;
  }

  template <class C>
  using member_pair =
    ::std::pair<C* const, R (C::* const)(A...)>;

  template <class C>
  using const_member_pair =
    ::std::pair<C const* const, R (C::* const)(A...) const>;

  template <class C>
  static arraydelegate from(C* const object_ptr,
    R (C::* const method_ptr)(A...))
  {
    return member_pair<C>(object_ptr, method_ptr);
  }

  template <class C>
  static arraydelegate from(C const* const object_ptr,
    R (C::* const method_ptr)(A...) const)
  {
    return const_member_pair<C>(object_ptr, method_ptr);
  }

  template <class C>
  static arraydelegate from(C& object, R (C::* const method_ptr)(A...))
  {
    return member_pair<C>(&object, method_ptr);
  }

  template <class C>
  static arraydelegate from(C const& object,
    R (C::* const method_ptr)(A...) const)
  {
    return const_member_pair<C>(&object, method_ptr);
  }

  void reset() noexcept { stub_ptr_ = nullptr; }

  void reset_stub() noexcept { stub_ptr_ = nullptr; }

  void swap(arraydelegate& other) noexcept { ::std::swap(*this, other); }

  bool operator==(arraydelegate const& rhs) const noexcept
  {
    return (object_ptr_ == rhs.object_ptr_) && (stub_ptr_ == rhs.stub_ptr_);
  }

  bool operator!=(arraydelegate const& rhs) const noexcept
  {
    return !operator==(rhs);
  }

  bool operator<(arraydelegate const& rhs) const noexcept
  {
    return (object_ptr_ < rhs.object_ptr_) ||
      ((object_ptr_ == rhs.object_ptr_) && (stub_ptr_ < rhs.stub_ptr_));
  }

  bool operator==(::std::nullptr_t const) const noexcept
  {
    return !stub_ptr_;
  }

  bool operator!=(::std::nullptr_t const) const noexcept
  {
    return stub_ptr_;
  }

  explicit operator bool() const noexcept { return stub_ptr_; }

  R operator()(A... args) const
  {
//  assert(stub_ptr);
    return stub_ptr_(object_ptr_, ::std::forward<A>(args)...);
  }

private:
  static void default_deleter_stub(void* const) { }

  template <class T>
  static void deleter_stub(void* const p)
  {
    static_cast<T*>(p)->~T();
  }

private:
  friend struct ::std::hash<arraydelegate>;

  using deleter_type = void (*)(void*);

  void* object_ptr_;
  stub_ptr_type stub_ptr_{};

  deleter_type deleter_{default_deleter_stub};

  alignas(size_t) char store_[max_store_size];

  template <R (*function_ptr)(A...)>
  static R function_stub(void* const, A&&... args)
  {
    return function_ptr(::std::forward<A>(args)...);
  }

  template <class C, R (C::*method_ptr)(A...)>
  static R method_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<C*>(object_ptr)->*method_ptr)(
      ::std::forward<A>(args)...);
  }

  template <class C, R (C::*method_ptr)(A...) const>
  static R const_method_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<C const*>(object_ptr)->*method_ptr)(
      ::std::forward<A>(args)...);
  }

  template <typename>
  struct is_member_pair : std::false_type { };

  template <class C>
  struct is_member_pair< ::std::pair<C* const,
    R (C::* const)(A...)> > : std::true_type
  {
  };

  template <typename>
  struct is_const_member_pair : std::false_type { };

  template <class C>
  struct is_const_member_pair< ::std::pair<C const* const,
    R (C::* const)(A...) const> > : std::true_type
  {
  };

  template <typename T>
  static typename ::std::enable_if<
    !(is_member_pair<T>{} ||
    is_const_member_pair<T>{}),
    R
  >::type
  functor_stub(void* const object_ptr, A&&... args)
  {
    return (*static_cast<T*>(object_ptr))(::std::forward<A>(args)...);
  }

  template <typename T>
  static typename ::std::enable_if<
    is_member_pair<T>{} ||
    is_const_member_pair<T>{},
    R
  >::type
  functor_stub(void* const object_ptr, A&&... args)
  {
    return (static_cast<T*>(object_ptr)->first->*
      static_cast<T*>(object_ptr)->second)(::std::forward<A>(args)...);
  }
};

}

namespace std
{
  template <typename R, typename ...A>
  struct hash<::sparks::arraydelegate<R (A...)> >
  {
    size_t operator()(
      ::sparks::arraydelegate<R (A...)> const& d) const noexcept
    {
      auto const seed(hash<void*>()(d.object_ptr_));

      return hash<
        typename ::sparks::arraydelegate<R (A...)>::stub_ptr_type>()(
          d.stub_ptr_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
  };
}

#endif // ARRAYDELEGATE_HPP
