// Compatibility wrapper for the fmt 6 copy vendored by mockturtle.
//
// With C++17, fmt falls back to an enum-based char8_type. Some libc++
// versions do not provide std::char_traits for that type, but fmt's
// u8string_view constructor instantiates it while parsing the header.

#ifndef CIRCT_MOCKTURTLE_FMT_FORMAT_WRAPPER_H
#define CIRCT_MOCKTURTLE_FMT_FORMAT_WRAPPER_H

#include <cstddef>
#include <cstring>
#include <ios>

#include_next <fmt/core.h>

#ifndef __cpp_char8_t
namespace std {
template <>
struct char_traits<fmt::internal::char8_type> {
  using char_type = fmt::internal::char8_type;
  using int_type = unsigned int;
  using off_type = streamoff;
  using pos_type = streampos;
  using state_type = mbstate_t;

  static void assign(char_type &r, const char_type &a) noexcept { r = a; }
  static constexpr bool eq(char_type a, char_type b) noexcept { return a == b; }
  static constexpr bool lt(char_type a, char_type b) noexcept { return a < b; }

  static int compare(const char_type *s1, const char_type *s2, size_t n) {
    for (; n != 0; --n, ++s1, ++s2) {
      if (lt(*s1, *s2))
        return -1;
      if (lt(*s2, *s1))
        return 1;
    }
    return 0;
  }

  static size_t length(const char_type *s) {
    size_t n = 0;
    while (!eq(s[n], char_type()))
      ++n;
    return n;
  }

  static const char_type *find(const char_type *s, size_t n,
                               const char_type &a) {
    for (; n != 0; --n, ++s)
      if (eq(*s, a))
        return s;
    return nullptr;
  }

  static char_type *move(char_type *s1, const char_type *s2, size_t n) {
    return static_cast<char_type *>(memmove(s1, s2, n * sizeof(char_type)));
  }

  static char_type *copy(char_type *s1, const char_type *s2, size_t n) {
    return static_cast<char_type *>(memcpy(s1, s2, n * sizeof(char_type)));
  }

  static char_type *assign(char_type *s, size_t n, char_type a) {
    for (size_t i = 0; i != n; ++i)
      s[i] = a;
    return s;
  }

  static constexpr int_type not_eof(int_type c) noexcept {
    return eq_int_type(c, eof()) ? 0 : c;
  }
  static constexpr char_type to_char_type(int_type c) noexcept {
    return static_cast<char_type>(c);
  }
  static constexpr int_type to_int_type(char_type c) noexcept {
    return static_cast<unsigned char>(c);
  }
  static constexpr bool eq_int_type(int_type c1, int_type c2) noexcept {
    return c1 == c2;
  }
  static constexpr int_type eof() noexcept {
    return static_cast<int_type>(-1);
  }
};
} // namespace std
#endif

#include_next <fmt/format.h>

#endif // CIRCT_MOCKTURTLE_FMT_FORMAT_WRAPPER_H
