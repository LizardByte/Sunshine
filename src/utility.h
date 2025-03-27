/**
 * @file src/utility.h
 * @brief Declarations for utility functions.
 */
#pragma once

// standard includes
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#define KITTY_WHILE_LOOP(x, y, z) \
  { \
    x; \
    while (y) z \
  }

template<typename T>
struct argument_type;

template<typename T, typename U>
struct argument_type<T(U)> {
  typedef U type;
};

#define KITTY_USING_MOVE_T(move_t, t, init_val, z) \
  class move_t { \
  public: \
    using element_type = typename argument_type<void(t)>::type; \
\
    move_t(): \
        el {init_val} { \
    } \
    template<class... Args> \
    move_t(Args &&...args): \
        el {std::forward<Args>(args)...} { \
    } \
    move_t(const move_t &) = delete; \
\
    move_t(move_t &&other) noexcept: \
        el {std::move(other.el)} { \
      other.el = element_type {init_val}; \
    } \
\
    move_t &operator=(const move_t &) = delete; \
\
    move_t &operator=(move_t &&other) { \
      std::swap(el, other.el); \
      return *this; \
    } \
    element_type *operator->() { \
      return &el; \
    } \
    const element_type *operator->() const { \
      return &el; \
    } \
\
    inline element_type release() { \
      element_type val = std::move(el); \
      el = element_type {init_val}; \
      return val; \
    } \
\
    ~move_t() z \
\
      element_type el; \
  }

#define KITTY_DECL_CONSTR(x) \
  x(x &&) noexcept = default; \
  x &operator=(x &&) noexcept = default; \
  x();

#define KITTY_DEFAULT_CONSTR_MOVE(x) \
  x(x &&) noexcept = default; \
  x &operator=(x &&) noexcept = default;

#define KITTY_DEFAULT_CONSTR_MOVE_THROW(x) \
  x(x &&) = default; \
  x &operator=(x &&) = default; \
  x() = default;

#define KITTY_DEFAULT_CONSTR(x) \
  KITTY_DEFAULT_CONSTR_MOVE(x) \
  x(const x &) noexcept = default; \
  x &operator=(const x &) = default;

#define TUPLE_2D(a, b, expr) \
  decltype(expr) a##_##b = expr; \
  auto &a = std::get<0>(a##_##b); \
  auto &b = std::get<1>(a##_##b)

#define TUPLE_2D_REF(a, b, expr) \
  auto &a##_##b = expr; \
  auto &a = std::get<0>(a##_##b); \
  auto &b = std::get<1>(a##_##b)

#define TUPLE_3D(a, b, c, expr) \
  decltype(expr) a##_##b##_##c = expr; \
  auto &a = std::get<0>(a##_##b##_##c); \
  auto &b = std::get<1>(a##_##b##_##c); \
  auto &c = std::get<2>(a##_##b##_##c)

#define TUPLE_3D_REF(a, b, c, expr) \
  auto &a##_##b##_##c = expr; \
  auto &a = std::get<0>(a##_##b##_##c); \
  auto &b = std::get<1>(a##_##b##_##c); \
  auto &c = std::get<2>(a##_##b##_##c)

#define TUPLE_EL(a, b, expr) \
  decltype(expr) a##_ = expr; \
  auto &a = std::get<b>(a##_)

#define TUPLE_EL_REF(a, b, expr) \
  auto &a = std::get<b>(expr)

namespace util {

  template<template<typename...> class X, class... Y>
  struct __instantiation_of: public std::false_type {};

  template<template<typename...> class X, class... Y>
  struct __instantiation_of<X, X<Y...>>: public std::true_type {};

  template<template<typename...> class X, class T, class... Y>
  static constexpr auto instantiation_of_v = __instantiation_of<X, T, Y...>::value;

  template<bool V, class X, class Y>
  struct __either;

  template<class X, class Y>
  struct __either<true, X, Y> {
    using type = X;
  };

  template<class X, class Y>
  struct __either<false, X, Y> {
    using type = Y;
  };

  template<bool V, class X, class Y>
  using either_t = typename __either<V, X, Y>::type;

  template<class... Ts>
  struct overloaded: Ts... {
    using Ts::operator()...;
  };
  template<class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;

  template<class T>
  class FailGuard {
  public:
    FailGuard() = delete;

    FailGuard(T &&f) noexcept:
        _func {std::forward<T>(f)} {
    }

    FailGuard(FailGuard &&other) noexcept:
        _func {std::move(other._func)} {
      this->failure = other.failure;

      other.failure = false;
    }

    FailGuard(const FailGuard &) = delete;

    FailGuard &operator=(const FailGuard &) = delete;
    FailGuard &operator=(FailGuard &&other) = delete;

    ~FailGuard() noexcept {
      if (failure) {
        _func();
      }
    }

    void disable() {
      failure = false;
    }

    bool failure {true};

  private:
    T _func;
  };

  template<class T>
  [[nodiscard]] auto fail_guard(T &&f) {
    return FailGuard<T> {std::forward<T>(f)};
  }

  template<class T>
  void append_struct(std::vector<uint8_t> &buf, const T &_struct) {
    constexpr size_t data_len = sizeof(_struct);

    buf.reserve(data_len);

    auto *data = (uint8_t *) &_struct;

    for (size_t x = 0; x < data_len; ++x) {
      buf.push_back(data[x]);
    }
  }

  template<class T>
  class Hex {
  public:
    typedef T elem_type;

  private:
    const char _bits[16] {
      '0',
      '1',
      '2',
      '3',
      '4',
      '5',
      '6',
      '7',
      '8',
      '9',
      'A',
      'B',
      'C',
      'D',
      'E',
      'F'
    };

    char _hex[sizeof(elem_type) * 2];

  public:
    Hex(const elem_type &elem, bool rev) {
      if (!rev) {
        const uint8_t *data = reinterpret_cast<const uint8_t *>(&elem) + sizeof(elem_type) - 1;
        for (auto it = begin(); it < cend();) {
          *it++ = _bits[*data / 16];
          *it++ = _bits[*data-- % 16];
        }
      } else {
        const uint8_t *data = reinterpret_cast<const uint8_t *>(&elem);
        for (auto it = begin(); it < cend();) {
          *it++ = _bits[*data / 16];
          *it++ = _bits[*data++ % 16];
        }
      }
    }

    char *begin() {
      return _hex;
    }

    char *end() {
      return _hex + sizeof(elem_type) * 2;
    }

    const char *begin() const {
      return _hex;
    }

    const char *end() const {
      return _hex + sizeof(elem_type) * 2;
    }

    const char *cbegin() const {
      return _hex;
    }

    const char *cend() const {
      return _hex + sizeof(elem_type) * 2;
    }

    std::string to_string() const {
      return {begin(), end()};
    }

    std::string_view to_string_view() const {
      return {begin(), sizeof(elem_type) * 2};
    }
  };

  template<class T>
  Hex<T> hex(const T &elem, bool rev = false) {
    return Hex<T>(elem, rev);
  }

  template<typename T>
  std::string log_hex(const T &value) {
    return "0x" + Hex<T>(value, false).to_string();
  }

  template<class It>
  std::string hex_vec(It begin, It end, bool rev = false) {
    auto str_size = 2 * std::distance(begin, end);

    std::string hex;
    hex.resize(str_size);

    const char _bits[16] {
      '0',
      '1',
      '2',
      '3',
      '4',
      '5',
      '6',
      '7',
      '8',
      '9',
      'A',
      'B',
      'C',
      'D',
      'E',
      'F'
    };

    if (rev) {
      for (auto it = std::begin(hex); it < std::end(hex);) {
        *it++ = _bits[((uint8_t) *begin) / 16];
        *it++ = _bits[((uint8_t) *begin++) % 16];
      }
    } else {
      --end;
      for (auto it = std::begin(hex); it < std::end(hex);) {
        *it++ = _bits[((uint8_t) *end) / 16];
        *it++ = _bits[((uint8_t) *end--) % 16];
      }
    }

    return hex;
  }

  template<class C>
  std::string hex_vec(C &&c, bool rev = false) {
    return hex_vec(std::begin(c), std::end(c), rev);
  }

  template<class T>
  T from_hex(const std::string_view &hex, bool rev = false) {
    std::uint8_t buf[sizeof(T)];

    static char constexpr shift_bit = 'a' - 'A';

    auto is_convertable = [](char ch) -> bool {
      if (isdigit(ch)) {
        return true;
      }

      ch |= shift_bit;

      if ('a' > ch || ch > 'z') {
        return false;
      }

      return true;
    };

    auto buf_size = std::count_if(std::begin(hex), std::end(hex), is_convertable) / 2;
    auto padding = sizeof(T) - buf_size;

    const char *data = hex.data() + hex.size() - 1;

    auto convert = [](char ch) -> std::uint8_t {
      if (ch >= '0' && ch <= '9') {
        return (std::uint8_t) ch - '0';
      }

      return (std::uint8_t)(ch | (char) 32) - 'a' + (char) 10;
    };

    std::fill_n(buf + buf_size, padding, 0);

    std::for_each_n(buf, buf_size, [&](auto &el) {
      while (!is_convertable(*data)) {
        --data;
      }
      std::uint8_t ch_r = convert(*data--);

      while (!is_convertable(*data)) {
        --data;
      }
      std::uint8_t ch_l = convert(*data--);

      el = (ch_l << 4) | ch_r;
    });

    if (rev) {
      std::reverse(std::begin(buf), std::end(buf));
    }

    return *reinterpret_cast<T *>(buf);
  }

  inline std::string from_hex_vec(const std::string &hex, bool rev = false) {
    std::string buf;

    static char constexpr shift_bit = 'a' - 'A';
    auto is_convertable = [](char ch) -> bool {
      if (isdigit(ch)) {
        return true;
      }

      ch |= shift_bit;

      if ('a' > ch || ch > 'z') {
        return false;
      }

      return true;
    };

    auto buf_size = std::count_if(std::begin(hex), std::end(hex), is_convertable) / 2;
    buf.resize(buf_size);

    const char *data = hex.data() + hex.size() - 1;

    auto convert = [](char ch) -> std::uint8_t {
      if (ch >= '0' && ch <= '9') {
        return (std::uint8_t) ch - '0';
      }

      return (std::uint8_t)(ch | (char) 32) - 'a' + (char) 10;
    };

    for (auto &el : buf) {
      while (!is_convertable(*data)) {
        --data;
      }
      std::uint8_t ch_r = convert(*data--);

      while (!is_convertable(*data)) {
        --data;
      }
      std::uint8_t ch_l = convert(*data--);

      el = (ch_l << 4) | ch_r;
    }

    if (rev) {
      std::reverse(std::begin(buf), std::end(buf));
    }

    return buf;
  }

  template<class T>
  class hash {
  public:
    using value_type = T;

    std::size_t operator()(const value_type &value) const {
      const auto *p = reinterpret_cast<const char *>(&value);

      return std::hash<std::string_view> {}(std::string_view {p, sizeof(value_type)});
    }
  };

  template<class T>
  auto enm(const T &val) -> const std::underlying_type_t<T> & {
    return *reinterpret_cast<const std::underlying_type_t<T> *>(&val);
  }

  template<class T>
  auto enm(T &val) -> std::underlying_type_t<T> & {
    return *reinterpret_cast<std::underlying_type_t<T> *>(&val);
  }

  inline std::int64_t from_chars(const char *begin, const char *end) {
    if (begin == end) {
      return 0;
    }

    std::int64_t res {};
    std::int64_t mul = 1;
    while (begin != --end) {
      res += (std::int64_t)(*end - '0') * mul;

      mul *= 10;
    }

    return *begin != '-' ? res + (std::int64_t)(*begin - '0') * mul : -res;
  }

  inline std::int64_t from_view(const std::string_view &number) {
    return from_chars(std::begin(number), std::end(number));
  }

  template<class X, class Y>
  class Either: public std::variant<std::monostate, X, Y> {
  public:
    using std::variant<std::monostate, X, Y>::variant;

    constexpr bool has_left() const {
      return std::holds_alternative<X>(*this);
    }

    constexpr bool has_right() const {
      return std::holds_alternative<Y>(*this);
    }

    X &left() {
      return std::get<X>(*this);
    }

    Y &right() {
      return std::get<Y>(*this);
    }

    const X &left() const {
      return std::get<X>(*this);
    }

    const Y &right() const {
      return std::get<Y>(*this);
    }
  };

  // Compared to std::unique_ptr, it adds the ability to get the address of the pointer itself
  template<typename T, typename D = std::default_delete<T>>
  class uniq_ptr {
  public:
    using element_type = T;
    using pointer = element_type *;
    using const_pointer = element_type const *;
    using deleter_type = D;

    constexpr uniq_ptr() noexcept:
        _p {nullptr} {
    }

    constexpr uniq_ptr(std::nullptr_t) noexcept:
        _p {nullptr} {
    }

    uniq_ptr(const uniq_ptr &other) noexcept = delete;
    uniq_ptr &operator=(const uniq_ptr &other) noexcept = delete;

    template<class V>
    uniq_ptr(V *p) noexcept:
        _p {p} {
      static_assert(std::is_same_v<element_type, void> || std::is_same_v<element_type, V> || std::is_base_of_v<element_type, V>, "element_type must be base class of V");
    }

    template<class V>
    uniq_ptr(std::unique_ptr<V, deleter_type> &&uniq) noexcept:
        _p {uniq.release()} {
      static_assert(std::is_same_v<element_type, void> || std::is_same_v<T, V> || std::is_base_of_v<element_type, V>, "element_type must be base class of V");
    }

    template<class V>
    uniq_ptr(uniq_ptr<V, deleter_type> &&other) noexcept:
        _p {other.release()} {
      static_assert(std::is_same_v<element_type, void> || std::is_same_v<T, V> || std::is_base_of_v<element_type, V>, "element_type must be base class of V");
    }

    template<class V>
    uniq_ptr &operator=(uniq_ptr<V, deleter_type> &&other) noexcept {
      static_assert(std::is_same_v<element_type, void> || std::is_same_v<T, V> || std::is_base_of_v<element_type, V>, "element_type must be base class of V");
      reset(other.release());

      return *this;
    }

    template<class V>
    uniq_ptr &operator=(std::unique_ptr<V, deleter_type> &&uniq) noexcept {
      static_assert(std::is_same_v<element_type, void> || std::is_same_v<T, V> || std::is_base_of_v<element_type, V>, "element_type must be base class of V");

      reset(uniq.release());

      return *this;
    }

    ~uniq_ptr() {
      reset();
    }

    void reset(pointer p = pointer()) {
      if (_p) {
        _deleter(_p);
      }

      _p = p;
    }

    pointer release() {
      auto tmp = _p;
      _p = nullptr;
      return tmp;
    }

    pointer get() {
      return _p;
    }

    const_pointer get() const {
      return _p;
    }

    std::add_lvalue_reference_t<element_type const> operator*() const {
      return *_p;
    }

    std::add_lvalue_reference_t<element_type> operator*() {
      return *_p;
    }

    const_pointer operator->() const {
      return _p;
    }

    pointer operator->() {
      return _p;
    }

    pointer *operator&() const {
      return &_p;
    }

    pointer *operator&() {
      return &_p;
    }

    deleter_type &get_deleter() {
      return _deleter;
    }

    const deleter_type &get_deleter() const {
      return _deleter;
    }

    explicit operator bool() const {
      return _p != nullptr;
    }

  protected:
    pointer _p;
    deleter_type _deleter;
  };

  template<class T1, class D1, class T2, class D2>
  bool operator==(const uniq_ptr<T1, D1> &x, const uniq_ptr<T2, D2> &y) {
    return x.get() == y.get();
  }

  template<class T1, class D1, class T2, class D2>
  bool operator!=(const uniq_ptr<T1, D1> &x, const uniq_ptr<T2, D2> &y) {
    return x.get() != y.get();
  }

  template<class T1, class D1, class T2, class D2>
  bool operator==(const std::unique_ptr<T1, D1> &x, const uniq_ptr<T2, D2> &y) {
    return x.get() == y.get();
  }

  template<class T1, class D1, class T2, class D2>
  bool operator!=(const std::unique_ptr<T1, D1> &x, const uniq_ptr<T2, D2> &y) {
    return x.get() != y.get();
  }

  template<class T1, class D1, class T2, class D2>
  bool operator==(const uniq_ptr<T1, D1> &x, const std::unique_ptr<T1, D1> &y) {
    return x.get() == y.get();
  }

  template<class T1, class D1, class T2, class D2>
  bool operator!=(const uniq_ptr<T1, D1> &x, const std::unique_ptr<T1, D1> &y) {
    return x.get() != y.get();
  }

  template<class T, class D>
  bool operator==(const uniq_ptr<T, D> &x, std::nullptr_t) {
    return !(bool) x;
  }

  template<class T, class D>
  bool operator!=(const uniq_ptr<T, D> &x, std::nullptr_t) {
    return (bool) x;
  }

  template<class T, class D>
  bool operator==(std::nullptr_t, const uniq_ptr<T, D> &y) {
    return !(bool) y;
  }

  template<class T, class D>
  bool operator!=(std::nullptr_t, const uniq_ptr<T, D> &y) {
    return (bool) y;
  }

  template<class P>
  using shared_t = std::shared_ptr<typename P::element_type>;

  template<class P, class T>
  shared_t<P> make_shared(T *pointer) {
    return shared_t<P>(reinterpret_cast<typename P::pointer>(pointer), typename P::deleter_type());
  }

  template<class T>
  class wrap_ptr {
  public:
    using element_type = T;
    using pointer = element_type *;
    using const_pointer = element_type const *;
    using reference = element_type &;
    using const_reference = element_type const &;

    wrap_ptr():
        _own_ptr {false},
        _p {nullptr} {
    }

    wrap_ptr(pointer p):
        _own_ptr {false},
        _p {p} {
    }

    wrap_ptr(std::unique_ptr<element_type> &&uniq_p):
        _own_ptr {true},
        _p {uniq_p.release()} {
    }

    wrap_ptr(wrap_ptr &&other):
        _own_ptr {other._own_ptr},
        _p {other._p} {
      other._own_ptr = false;
    }

    wrap_ptr &operator=(wrap_ptr &&other) noexcept {
      if (_own_ptr) {
        delete _p;
      }

      _p = other._p;

      _own_ptr = other._own_ptr;
      other._own_ptr = false;

      return *this;
    }

    template<class V>
    wrap_ptr &operator=(std::unique_ptr<V> &&uniq_ptr) {
      static_assert(std::is_base_of_v<element_type, V>, "element_type must be base class of V");
      _own_ptr = true;
      _p = uniq_ptr.release();

      return *this;
    }

    wrap_ptr &operator=(pointer p) {
      if (_own_ptr) {
        delete _p;
      }

      _p = p;
      _own_ptr = false;

      return *this;
    }

    ~wrap_ptr() {
      if (_own_ptr) {
        delete _p;
      }

      _own_ptr = false;
    }

    const_reference operator*() const {
      return *_p;
    }

    reference operator*() {
      return *_p;
    }

    const_pointer operator->() const {
      return _p;
    }

    pointer operator->() {
      return _p;
    }

  private:
    bool _own_ptr;
    pointer _p;
  };

  template<class T>
  constexpr bool is_pointer_v =
    instantiation_of_v<std::unique_ptr, T> ||
    instantiation_of_v<std::shared_ptr, T> ||
    instantiation_of_v<uniq_ptr, T> ||
    std::is_pointer_v<T>;

  template<class T, class V = void>
  struct __false_v;

  template<class T>
  struct __false_v<T, std::enable_if_t<instantiation_of_v<std::optional, T>>> {
    static constexpr std::nullopt_t value = std::nullopt;
  };

  template<class T>
  struct __false_v<T, std::enable_if_t<is_pointer_v<T>>> {
    static constexpr std::nullptr_t value = nullptr;
  };

  template<class T>
  struct __false_v<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static constexpr bool value = false;
  };

  template<class T>
  static constexpr auto false_v = __false_v<T>::value;

  template<class T>
  using optional_t = either_t<
    (std::is_same_v<T, bool> || is_pointer_v<T>),
    T,
    std::optional<T>>;

  template<class T>
  class buffer_t {
  public:
    buffer_t():
        _els {0} {};

    buffer_t(buffer_t &&o) noexcept:
        _els {o._els},
        _buf {std::move(o._buf)} {
      o._els = 0;
    }

    buffer_t(const buffer_t &o):
        _els {o._els},
        _buf {std::make_unique<T[]>(_els)} {
      std::copy(o.begin(), o.end(), begin());
    }

    buffer_t &operator=(buffer_t &&o) noexcept {
      std::swap(_els, o._els);
      std::swap(_buf, o._buf);

      return *this;
    };

    explicit buffer_t(size_t elements):
        _els {elements},
        _buf {std::make_unique<T[]>(elements)} {
    }

    explicit buffer_t(size_t elements, const T &t):
        _els {elements},
        _buf {std::make_unique<T[]>(elements)} {
      std::fill_n(_buf.get(), elements, t);
    }

    T &operator[](size_t el) {
      return _buf[el];
    }

    const T &operator[](size_t el) const {
      return _buf[el];
    }

    size_t size() const {
      return _els;
    }

    void fake_resize(std::size_t els) {
      _els = els;
    }

    T *begin() {
      return _buf.get();
    }

    const T *begin() const {
      return _buf.get();
    }

    T *end() {
      return _buf.get() + _els;
    }

    const T *end() const {
      return _buf.get() + _els;
    }

  private:
    size_t _els;
    std::unique_ptr<T[]> _buf;
  };

  template<class T>
  T either(std::optional<T> &&l, T &&r) {
    if (l) {
      return std::move(*l);
    }

    return std::forward<T>(r);
  }

  template<class ReturnType, class... Args>
  struct Function {
    typedef ReturnType (*type)(Args...);
  };

  template<class T, class ReturnType, typename Function<ReturnType, T>::type function>
  struct Destroy {
    typedef T pointer;

    void operator()(pointer p) {
      function(p);
    }
  };

  template<class T, typename Function<void, T *>::type function>
  using safe_ptr = uniq_ptr<T, Destroy<T *, void, function>>;

  // You cannot specialize an alias
  template<class T, class ReturnType, typename Function<ReturnType, T *>::type function>
  using safe_ptr_v2 = uniq_ptr<T, Destroy<T *, ReturnType, function>>;

  template<class T>
  void c_free(T *p) {
    free(p);
  }

  template<class T, class ReturnType, ReturnType (**function)(T *)>
  void dynamic(T *p) {
    (*function)(p);
  }

  template<class T, void (**function)(T *)>
  using dyn_safe_ptr = safe_ptr<T, dynamic<T, void, function>>;

  template<class T, class ReturnType, ReturnType (**function)(T *)>
  using dyn_safe_ptr_v2 = safe_ptr<T, dynamic<T, ReturnType, function>>;

  template<class T>
  using c_ptr = safe_ptr<T, c_free<T>>;

  template<class It>
  std::string_view view(It begin, It end) {
    return std::string_view {(const char *) begin, (std::size_t)(end - begin)};
  }

  template<class T>
  std::string_view view(const T &data) {
    return std::string_view((const char *) &data, sizeof(T));
  }

  struct point_t {
    double x;
    double y;

    friend std::ostream &operator<<(std::ostream &os, const point_t &p) {
      return (os << "Point(x: " << p.x << ", y: " << p.y << ")");
    }
  };

  namespace endian {
    template<class T = void>
    struct endianness {
      enum : bool {
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || \
  defined(__BIG_ENDIAN__) || \
  defined(__ARMEB__) || \
  defined(__THUMBEB__) || \
  defined(__AARCH64EB__) || \
  defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
        // It's a big-endian target architecture
        little = false,
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
  defined(__LITTLE_ENDIAN__) || \
  defined(__ARMEL__) || \
  defined(__THUMBEL__) || \
  defined(__AARCH64EL__) || \
  defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
  defined(_WIN32)
        little = true,  ///< little-endian target architecture
#else
  #error "Unknown Endianness"
#endif
        big = !little  ///< big-endian target architecture
      };
    };

    template<class T, class S = void>
    struct endian_helper {};

    template<class T>
    struct endian_helper<T, std::enable_if_t<!(instantiation_of_v<std::optional, T>)>> {
      static inline T big(T x) {
        if constexpr (endianness<T>::little) {
          uint8_t *data = reinterpret_cast<uint8_t *>(&x);

          std::reverse(data, data + sizeof(x));
        }

        return x;
      }

      static inline T little(T x) {
        if constexpr (endianness<T>::big) {
          uint8_t *data = reinterpret_cast<uint8_t *>(&x);

          std::reverse(data, data + sizeof(x));
        }

        return x;
      }
    };

    template<class T>
    struct endian_helper<T, std::enable_if_t<instantiation_of_v<std::optional, T>>> {
      static inline T little(T x) {
        if (!x) {
          return x;
        }

        if constexpr (endianness<T>::big) {
          auto *data = reinterpret_cast<uint8_t *>(&*x);

          std::reverse(data, data + sizeof(*x));
        }

        return x;
      }

      static inline T big(T x) {
        if (!x) {
          return x;
        }

        if constexpr (endianness<T>::little) {
          auto *data = reinterpret_cast<uint8_t *>(&*x);

          std::reverse(data, data + sizeof(*x));
        }

        return x;
      }
    };

    template<class T>
    inline auto little(T x) {
      return endian_helper<T>::little(x);
    }

    template<class T>
    inline auto big(T x) {
      return endian_helper<T>::big(x);
    }
  }  // namespace endian
}  // namespace util
