//
// Copyright (c) 2018 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#if not defined(__cpp_variadic_templates) or                                   \
    not defined(__cpp_rvalue_references) or not defined(__cpp_decltype) or     \
    not defined(__cpp_alias_templates) or                                      \
    not defined(__cpp_generic_lambdas) or not defined(__cpp_constexpr) or      \
    not defined(__cpp_return_type_deduction) or                                \
    not defined(__cpp_fold_expressions) or not defined(__cpp_static_assert) or \
    not defined(__cpp_delegating_constructors)
#error "Type.Erasure requires C++17 support"
#else
#pragma GCC system_header
#include <type_traits>
#include <utility>

namespace te {
inline namespace v1 {
namespace detail {
template <class...>
struct type_list {};

template <class, std::size_t>
struct mappings final {
  friend auto get(mappings);
  template <class T>
  struct set {
    friend auto get(mappings) { return T{}; }
  };
};

template <std::size_t, class...>
constexpr std::size_t mappings_size_impl(...) {
  return {};
}

template <std::size_t N, class T, class... Ts>
constexpr auto mappings_size_impl(bool dummy)
    -> decltype(get(mappings<T, N>{}), std::size_t{}) {
  return 1 + mappings_size_impl<N + 1, T, Ts...>(dummy);
}

template <class... Ts>
constexpr auto mappings_size() {
  return mappings_size_impl<1, Ts...>(bool{});
}

template <class T, class = decltype(sizeof(T))>
std::true_type is_complete_impl(bool);
template <class>
std::false_type is_complete_impl(...);
template <class T>
struct is_complete : decltype(is_complete_impl<T>(bool{})) {};

template <class T>
constexpr auto requires__(bool)
    -> decltype(std::declval<T>().template requires__<T>());
template <class>
constexpr auto requires__(...) -> void;

template <class TExpr>
class expr_wrapper final {
  static_assert(std::is_empty<TExpr>{});

 public:
  template <class... Ts>
  decltype(auto) operator()(Ts &&... args) const {
    return reinterpret_cast<const TExpr &>(*this)(std::forward<Ts>(args)...);
  }
};

class void_ptr final {
 public:
  using ptr_t = void *;
  using del_t = void (*)(ptr_t);
  using copy_t = ptr_t (*)(ptr_t);

 public:
  template <class T>
  constexpr explicit void_ptr(
      T *ptr = nullptr,
      del_t del = [](ptr_t ptr) { delete static_cast<T *>(ptr); },
      copy_t copy = [](ptr_t ptr) -> void * {
        return new T{*static_cast<T *>(ptr)};
      }) noexcept
      : ptr{ptr}, del{del}, copy{copy} {}

  constexpr void_ptr(const void_ptr &other) noexcept
      : ptr{other.copy(other.ptr)}, del{other.del}, copy{other.copy} {}

  constexpr void_ptr(void_ptr &&other) noexcept
      : ptr{std::move(other.ptr)},
        del{std::move(other.del)},
        copy{std::move(other.copy)} {
    other.ptr = nullptr;
  }

  constexpr void_ptr &operator=(const void_ptr &other) noexcept {
    reset(other.copy(other.ptr));
    del = other.del;
    copy = other.copy;
    return *this;
  }

  constexpr void_ptr &operator=(void_ptr &&other) noexcept {
    reset(std::move(other.ptr));
    del = std::move(other.del);
    copy = std::move(other.copy);
    other.ptr = nullptr;
    return *this;
  }

  ~void_ptr() noexcept { reset(); }

  constexpr void reset(ptr_t new_ptr = {}) noexcept {
    del(ptr);
    ptr = new_ptr;
  }

  template <class T = void>
  constexpr decltype(auto) get() const noexcept {
    return reinterpret_cast<T *>(ptr);
  }

 private:
  ptr_t ptr;
  del_t del;
  copy_t copy;
};
}  // namespace detail

class dynamic_storage {
 public:
  template <class T, class T_ = std::decay_t<T> >
  constexpr explicit dynamic_storage(T &&t) noexcept
      : ptr{new T_{std::forward<T>(t)}} {}
  constexpr decltype(auto) get() const noexcept { return ptr.get(); }

 private:
  detail::void_ptr ptr;  // layout
};

template <std::size_t Size, std::size_t Alignment = 16>
class local_storage {
 public:
  template <class T, class T_ = std::decay_t<T> >
  constexpr explicit local_storage(T &&t) noexcept
      : ptr{&data, [](void *ptr) { static_cast<T_ *>(ptr)->~T_(); },
            [](void *ptr) -> void * {
              return new (ptr) T_{*static_cast<T_ *>(ptr)};
            }} {
    static_assert(sizeof(T) <= Size);
    new (&data) T_{std::forward<T>(t)};
  }

  constexpr decltype(auto) get() const noexcept { return &data; }

 private:
  detail::void_ptr ptr;  // layout
  std::aligned_storage<Size, Alignment> data;
};

class static_vtable {
  using ptr_t = void *;

 public:
  template <class T, std::size_t Size>
  static_vtable(T &&, std::integral_constant<std::size_t, Size>) noexcept {
    static ptr_t vt[Size]{};
    vtable = vt;
  }
  decltype(auto) operator[](std::size_t index) const noexcept {
    return vtable[index];
  }

 private:
  ptr_t *vtable{};  // layout
};

template <class I, class TStorage = dynamic_storage,
          class TVtable = static_vtable>
class poly : public std::conditional_t<detail::is_complete<I>{}, I,
                                       detail::type_list<I> > {
 public:
  template <class T,
            class = std::enable_if_t<not std::is_convertible<T, poly>{} and
                                     std::is_copy_constructible<T>{} and
                                     std::is_destructible<T>{}> >
  constexpr poly(T &&t) noexcept
      : poly{std::forward<T>(t),
             detail::type_list<decltype(detail::requires__<I>(bool{}))>{}} {}
  constexpr poly(poly const &) noexcept = default;
  constexpr poly &operator=(poly const &) noexcept = default;
  constexpr poly(poly &&) noexcept = default;
  constexpr poly &operator=(poly &&) noexcept = default;

 private:
  template <class T, class TRequires>
  constexpr poly(T &&t, const TRequires) noexcept
      : poly{std::forward<T>(t),
             std::make_index_sequence<detail::mappings_size<I>()>{}} {}

  template <class T, std::size_t... Ns>
  constexpr poly(T &&t, std::index_sequence<Ns...>) noexcept
      : vtable{std::forward<T>(t),
               std::integral_constant<std::size_t, sizeof...(Ns)>{}},
        storage{std::forward<T>(t)} {
    static_assert(sizeof...(Ns) > 0);
    (init<Ns + 1, std::decay_t<T> >(
         decltype(get(detail::mappings<I, Ns + 1>{})){}),
     ...);
  }

  template <std::size_t N, class T, class TExpr, class... TArgs>
  constexpr void init(detail::type_list<TExpr, TArgs...>) noexcept {
    vtable[N - 1] = reinterpret_cast<void *>(+[](void *self, TArgs... args) {
      return detail::expr_wrapper<TExpr>{}(*static_cast<T *>(self), args...);
    });
  }

  template <std::size_t N, class R, class TExpr, class... Ts>
  friend constexpr auto call(const poly &self,
                             std::integral_constant<std::size_t, N>,
                             detail::type_list<R>, const TExpr,
                             Ts &&... args) noexcept {
    void(typename detail::mappings<I, N>::template set<
         detail::type_list<TExpr, Ts...> >{});
    return reinterpret_cast<R (*)(void *, Ts...)>(self.vtable[N - 1])(
        self.storage.get(), std::forward<Ts>(args)...);
  }

  TVtable vtable;
  TStorage storage;
};

template <class R = void, std::size_t N = 0, class TExpr, class I, class... Ts>
constexpr auto call(const TExpr expr, const I &interface,
                    Ts &&... args) noexcept {
  static_assert(std::is_empty<TExpr>{});
  return call(
      static_cast<const poly<I> &>(interface),
      std::integral_constant<std::size_t,
                             detail::mappings_size<I, class call>() + 1>{},
      detail::type_list<R>{}, expr, std::forward<Ts>(args)...);
}

namespace detail {
template <class I, class T, std::size_t... Ns>
constexpr auto extends_impl(std::index_sequence<Ns...>) noexcept {
  (void(typename mappings<T, Ns + 1>::template set<decltype(
            get(mappings<I, Ns + 1>{}))>{}),
   ...);
}
}  // namespace detail

template <class I, class T>
constexpr auto extends(const T &) noexcept {
  detail::extends_impl<I, T>(
      std::make_index_sequence<detail::mappings_size<I, T>()>{});
}

}  // namespace v1
}  // namespace te
#endif