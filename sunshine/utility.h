#ifndef UTILITY_H
#define UTILITY_H

#include <variant>
#include <vector>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <string_view>

#define KITTY_WHILE_LOOP(x, y, z) { x;while(y) z }
#define KITTY_DECL_CONSTR(x)\
  x(x&&) noexcept = default;\
  x&operator=(x&&) noexcept = default;\
  x();

#define KITTY_DEFAULT_CONSTR(x)\
  x(x&&) noexcept = default;\
  x&operator=(x&&) noexcept = default;\
  x() = default;

#define KITTY_DEFAULT_CONSTR_THROW(x)\
  x(x&&) = default;\
  x&operator=(x&&) = default;\
  x() = default;

#define TUPLE_2D(a,b, expr)\
  decltype(expr) a##_##b = expr;\
  auto &a = std::get<0>(a##_##b);\
  auto &b = std::get<1>(a##_##b)

#define TUPLE_2D_REF(a,b, expr)\
  auto &a##_##b = expr;\
  auto &a = std::get<0>(a##_##b);\
  auto &b = std::get<1>(a##_##b)

#define TUPLE_3D(a,b,c, expr)\
  decltype(expr) a##_##b##_##c = expr;\
  auto &a = std::get<0>(a##_##b##_##c);\
  auto &b = std::get<1>(a##_##b##_##c);\
  auto &c = std::get<2>(a##_##b##_##c)

#define TUPLE_3D_REF(a,b,c, expr)\
  auto &a##_##b##_##c = expr;\
  auto &a = std::get<0>(a##_##b##_##c);\
  auto &b = std::get<1>(a##_##b##_##c);\
  auto &c = std::get<2>(a##_##b##_##c)

namespace util {

template<template<typename...> class X, class...Y>
struct __instantiation_of : public std::false_type {};

template<template<typename...> class X, class... Y>
struct __instantiation_of<X, X<Y...>> : public std::true_type {};

template<template<typename...> class X, class T, class...Y>
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

template<class T, class V = void>
struct __false_v;

template<class T>
struct __false_v<T, std::enable_if_t<instantiation_of_v<std::optional, T>>> {
  static constexpr std::nullopt_t value = std::nullopt;
};

template<class T>
struct __false_v<T, std::enable_if_t<
  (std::is_pointer_v<T> || instantiation_of_v<std::unique_ptr, T> || instantiation_of_v<std::shared_ptr, T>)
  >> {
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
  (std::is_same_v<T, bool> ||
   instantiation_of_v<std::unique_ptr, T> ||
   instantiation_of_v<std::shared_ptr, T> ||
   std::is_pointer_v<T>),
  T, std::optional<T>>;

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

template<class T>
class FailGuard {
public:
  FailGuard() = delete;
  FailGuard(T && f) noexcept : _func { std::forward<T>(f) } {}
  FailGuard(FailGuard &&other) noexcept : _func { std::move(other._func) } {
    this->failure = other.failure;

    other.failure = false;
  }

  FailGuard(const FailGuard &) = delete;

  FailGuard &operator=(const FailGuard &) = delete;
  FailGuard &operator=(FailGuard &&other) = delete;

  ~FailGuard() noexcept {
    if(failure) {
      _func();
    }
  }

  void disable() { failure = false; }
  bool failure { true };
private:
  T _func;
};

template<class T>
[[nodiscard]] auto fail_guard(T && f) {
  return FailGuard<T> { std::forward<T>(f) };
}

template<class T>
void append_struct(std::vector<uint8_t> &buf, const T &_struct) {
  constexpr size_t data_len = sizeof(_struct);

  buf.reserve(data_len);

  auto *data = (uint8_t *) & _struct;

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
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
  };

  char _hex[sizeof(elem_type) * 2];
public:
  Hex(const elem_type &elem, bool rev) {
    if(!rev) {
      const uint8_t *data = reinterpret_cast<const uint8_t *>(&elem) + sizeof(elem_type) - 1;
      for (auto it = begin(); it < cend();) {
        *it++ = _bits[*data / 16];
        *it++ = _bits[*data-- % 16];
      }
    }
    else {
      const uint8_t *data = reinterpret_cast<const uint8_t *>(&elem);
      for (auto it = begin(); it < cend();) {
        *it++ = _bits[*data / 16];
        *it++ = _bits[*data++ % 16];
      }
    }
  }

  char *begin() { return _hex; }
  char *end() { return _hex + sizeof(elem_type) * 2; }

  const char *begin() const { return _hex; }
  const char *end() const { return _hex + sizeof(elem_type) * 2; }

  const char *cbegin() const { return _hex; }
  const char *cend() const { return _hex + sizeof(elem_type) * 2; }

  std::string to_string() const {
    return { begin(), end() };
  }

  std::string_view to_string_view() const {
    return { begin(), sizeof(elem_type) * 2 };
  }
};

template<class T>
Hex<T> hex(const T &elem, bool rev = false) {
  return Hex<T>(elem, rev);
}

template<class It>
std::string hex_vec(It begin, It end, bool rev = false) {
  auto str_size = 2*std::distance(begin, end);


  std::string hex;
  hex.resize(str_size);

  const char _bits[16] {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
  };

  if(rev) {
    for (auto it = std::begin(hex); it < std::end(hex);) {
      *it++ = _bits[((uint8_t)*begin) / 16];
      *it++ = _bits[((uint8_t)*begin++) % 16];
    }
  }
  else {
    --end;
    for (auto it = std::begin(hex); it < std::end(hex);) {
      *it++ = _bits[((uint8_t)*end) / 16];
      *it++ = _bits[((uint8_t)*end--) % 16];
    }
  }


  return hex;
}

template<class C>
std::string hex_vec(C&& c, bool rev = false) {
  return hex_vec(std::begin(c), std::end(c), rev);
}

template<class T>
std::optional<T> from_hex(const std::string_view &hex, bool rev = false) {
  std::uint8_t buf[sizeof(T)];

  static char constexpr shift_bit = 'a' - 'A';
  auto is_convertable = [] (char ch) -> bool {
    if(isdigit(ch)) {
      return true;
    }

    ch |= shift_bit;

    if('a' > ch || ch > 'z') {
      return false;
    }

    return true;
  };

  auto buf_size = std::count_if(std::begin(hex), std::end(hex), is_convertable) / 2;
  if(buf_size != sizeof(T)) {
    return std::nullopt;
  }

  const char *data = hex.data() + hex.size() -1;

  auto convert = [] (char ch) -> std::uint8_t {
    if(ch >= '0' && ch <= '9') {
      return (std::uint8_t)ch - '0';
    }

    return (std::uint8_t)(ch | (char)32) - 'a' + (char)10;
  };

  for(auto &el : buf) {
    while(!is_convertable(*data)) { --data; }
    std::uint8_t ch_r = convert(*data--);

    while(!is_convertable(*data)) { --data; }
    std::uint8_t ch_l = convert(*data--);

    el = (ch_l << 4) | ch_r;
  }

  if(rev) {
    std::reverse(std::begin(buf), std::end(buf));
  }

  return *reinterpret_cast<T *>(buf);
}

inline std::string from_hex_vec(const std::string &hex, bool rev = false) {
  std::string buf;

  static char constexpr shift_bit = 'a' - 'A';
  auto is_convertable = [] (char ch) -> bool {
    if(isdigit(ch)) {
      return true;
    }

    ch |= shift_bit;

    if('a' > ch || ch > 'z') {
      return false;
    }

    return true;
  };

  auto buf_size = std::count_if(std::begin(hex), std::end(hex), is_convertable) / 2;
  buf.resize(buf_size);

  const char *data = hex.data() + hex.size() -1;

  auto convert = [] (char ch) -> std::uint8_t {
    if(ch >= '0' && ch <= '9') {
      return (std::uint8_t)ch - '0';
    }

    return (std::uint8_t)(ch | (char)32) - 'a' + (char)10;
  };

  for(auto &el : buf) {
    while(!is_convertable(*data)) { --data; }
    std::uint8_t ch_r = convert(*data--);

    while(!is_convertable(*data)) { --data; }
    std::uint8_t ch_l = convert(*data--);

    el = (ch_l << 4) | ch_r;
  }

  if(rev) {
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

    return std::hash<std::string_view>{}(std::string_view { p, sizeof(value_type) });
  }
};

template<class T>
auto enm(const T& val) -> const std::underlying_type_t<T>& {
  return *reinterpret_cast<const std::underlying_type_t<T>*>(&val);
}

template<class T>
auto enm(T& val) -> std::underlying_type_t<T>& {
  return *reinterpret_cast<std::underlying_type_t<T>*>(&val);
}

template<class ReturnType, class ...Args>
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

template<class T, typename Function<void, T*>::type function>
using safe_ptr = std::unique_ptr<T, Destroy<T*, void, function>>;

// You cannot specialize an alias
template<class T, class ReturnType, typename Function<ReturnType, T*>::type function>
using safe_ptr_v2 = std::unique_ptr<T, Destroy<T*, ReturnType, function>>;

template<class T>
void c_free(T *p) {
  free(p);
}

template<class T>
using c_ptr = safe_ptr<T, c_free<T>>;

inline std::int64_t from_chars(const char *begin, const char *end) {
  std::int64_t res {};
  std::int64_t mul = 1;
  while(begin != --end) {
    res += (std::int64_t)(*end - '0') * mul;

    mul *= 10;
  }

  return *begin != '-' ? res + (std::int64_t)(*begin - '0') * mul : -res;
}

inline std::int64_t from_view(const std::string_view &number) {
  return from_chars(std::begin(number), std::end(number));
}

template<class X, class Y>
class Either : public std::variant<X, Y> {
public:
  using std::variant<X, Y>::variant;

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


template<class T>
class wrap_ptr {
public:
  using element_type = T;
  using pointer = element_type*;
  using reference = element_type&;

  wrap_ptr() : _own_ptr { false }, _p { nullptr } {}
  wrap_ptr(pointer p) : _own_ptr { false }, _p { p } {}
  wrap_ptr(std::unique_ptr<element_type> &&uniq_p) : _own_ptr { true }, _p { uniq_p.release() } {}
  wrap_ptr(wrap_ptr &&other) : _own_ptr { other._own_ptr }, _p { other._p } {
    other._own_ptr = false;
  }

  wrap_ptr &operator=(wrap_ptr &&other) noexcept {
    if(_own_ptr) {
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
    if(_own_ptr) {
      delete _p;
    }

    _p = p;
    _own_ptr = false;

    return *this;
  }

  ~wrap_ptr() {
    if(_own_ptr) {
      delete _p;
    }

    _own_ptr = false;
  }

  const reference operator*() const {
    return *_p;
  }
  reference operator*() {
    return *_p;
  }
  const pointer operator->() const {
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
class buffer_t {
public:
  buffer_t() : _els { 0 } {};
  buffer_t(buffer_t&&) noexcept = default;
  buffer_t &operator=(buffer_t&& other) noexcept = default;

  explicit buffer_t(size_t elements) : _els { elements }, _buf { std::make_unique<T[]>(elements) } {}
  explicit buffer_t(size_t elements, const T &t) : _els { elements }, _buf { std::make_unique<T[]>(elements) } {
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
  if(l) {
    return std::move(*l);
  }

  return std::forward<T>(r);
}

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
    // It's a little-endian target architecture
      little = true,
#else
#error "Unknown Endianness"
#endif
    big = !little
  };
};

template<class T, class S = void>
struct endian_helper { };

template<class T>
struct endian_helper<T, std::enable_if_t<
  !(instantiation_of_v<std::optional, T>)
>> {
  static inline T big(T x) {
    if constexpr (endianness<T>::little) {
      uint8_t *data = reinterpret_cast<uint8_t*>(&x);

      std::reverse(data, data + sizeof(x));
    }

    return x;
  }

  static inline T little(T x) {
    if constexpr (endianness<T>::big) {
      uint8_t *data = reinterpret_cast<uint8_t*>(&x);

      std::reverse(data, data + sizeof(x));
    }

    return x;
  }
};

template<class T>
struct endian_helper<T, std::enable_if_t<
  instantiation_of_v<std::optional, T>
>> {
static inline T little(T x) {
  if(!x) return x;

  if constexpr (endianness<T>::big) {
    auto *data = reinterpret_cast<uint8_t*>(&*x);

    std::reverse(data, data + sizeof(*x));
  }

  return x;
}


static inline T big(T x) {
  if(!x) return x;

  if constexpr (endianness<T>::big) {
    auto *data = reinterpret_cast<uint8_t*>(&*x);

    std::reverse(data, data + sizeof(*x));
  }

  return x;
}
};

template<class T>
inline auto little(T x) { return endian_helper<T>::little(x); }

template<class T>
inline auto big(T x) { return endian_helper<T>::big(x); }
} /* endian */

} /* util */
#endif
