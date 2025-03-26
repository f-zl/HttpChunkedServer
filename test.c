#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const char DIGITS[16] = "0123456789abcdef";
static char decimal_digit(int value) {
  assert(value >= 0 && value <= 9);
  return DIGITS[value];
}
static size_t num_to_chars(int value, char s[4]) {
  assert(value <= 9999 && value >= 0);
  // 0-9
  // 10-99
  // 100-999
  // 1000-9999
  if (value <= 9) {
    s[0] = decimal_digit(value);
    return 1;
  }
  int one, ten, hundred, thousand;
  if (value <= 99) {
    ten = value / 10;
    one = value % 10;
    s[0] = decimal_digit(ten);
    s[1] = decimal_digit(one);
    return 2;
  }
  if (value <= 999) {
    one = value % 10;
    value /= 10;
    ten = value % 10;
    hundred = value / 10;
    s[0] = decimal_digit(hundred);
    s[1] = decimal_digit(ten);
    s[2] = decimal_digit(one);
    return 3;
  }
  one = value % 10;
  value /= 10;
  ten = value % 10;
  value /= 10;
  hundred = value % 10;
  thousand = value / 10;
  s[0] = decimal_digit(thousand);
  s[1] = decimal_digit(hundred);
  s[2] = decimal_digit(ten);
  s[3] = decimal_digit(one);
  return 4;
}
#define CHECK(value) check(value, #value, strlen(#value))
static void check(int value, const char *s, size_t len) {
  printf("check(%d, %s, %zu)\n", value, s, len);
  char c[4];
  size_t l = num_to_chars(value, c);
  if (l != len) {
    printf("len %zu != %zu\n", l, len);
    return;
  }
  if (memcmp(s, c, len) != 0) {
    printf("Wrong result %d %s\n", value, s);
  }
}
typedef struct {
  bool has_value;
  uint16_t value;
} OptionU16;
static OptionU16 lower_hex_to_u16(const char str[4]) {
  // 长度4的字符串，每个都是小写十六进制字符
  OptionU16 r = {0};
  for (int i = 0; i < 4; ++i) {
    if (str[i] >= '0' && str[i] <= '9') {
      r.value += (str[i] - '0') << ((3 - i) * 4);
    } else if (str[i] >= 'a' && str[i] <= 'f') {
      r.value += (str[i] - 'a' + 10) << ((3 - i) * 4);
    } else {
      return r; // has_value = 0
    }
  }
  r.has_value = true;
  return r;
}
static void check1(const char str[4], bool has_value, uint16_t value) {
  OptionU16 u = lower_hex_to_u16(str);
  if (u.has_value) {
    if (!has_value) {
      printf("Wrong %*.s has_value 1!=0\n", 4, str);
      return;
    }
    if (u.value != value) {
      printf("Wrong %*.s value %04x!=%04x\n", 4, str, u.value, value);
    }
  } else {
    if (has_value) {
      printf("Wrong %*.s has_value 0!=1\n", 4, str);
    }
  }
}
static void test_lower_hex_to_u16() {
  check1("1234", 1, 0x1234);
  check1("00fa", 1, 0x00fa);
  check1("fbcd", 1, 0xfbcd);
  check1("0-al", 0, 0);
  check1("0ABF", 0, 0);
}
static uint64_t chars_to_num(const char *s, size_t len) {
  if (len > 7 || len == 0) {
    return -1;
  }
  uint64_t value = 0;
  uint64_t rank = 1;
  const size_t end = len - 1;
  for (size_t i = 0; i < len; ++i) {
    char c = s[end - i]; // 从后往前
    if (c >= '0' && c <= '9') {
      value += (c - '0') * rank;
      rank *= 10;
    } else {
      return -1;
    }
  }
  return value;
}
#define CHECK2(value) check2(#value, value)
static void check2(const char *s, int32_t expect) {
  int32_t actual = chars_to_num(s, strlen(s));
  if (actual != expect) {
    printf("Fail to parse %s %" PRIi32 "!=%" PRIi32 "\n", s, actual, expect);
  }
}
static void test_chars_to_num() {
  CHECK2(124);
  CHECK2(456);
  CHECK2(4194304);
  check2("1918276", 1918276);
  check2("19182761", -1);
  check2("1a", -1);
}
int main() {
  CHECK(0);
  CHECK(1);
  CHECK(9);
  CHECK(10);
  CHECK(24);
  CHECK(99);
  CHECK(100);
  CHECK(286);
  CHECK(757);
  CHECK(999);
  CHECK(1000);
  CHECK(1001);
  CHECK(5634);
  CHECK(8765);
  CHECK(9999);
  test_lower_hex_to_u16();
  test_chars_to_num();
  puts("Done");
}
