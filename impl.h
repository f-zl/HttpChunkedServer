#pragma once
// 提取chunked server的一些utility函数，放到头文件便于测试
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  bool has_value;
  uint16_t value;
} OptionU16;
static const char DIGITS[16] = "0123456789abcdef";
static inline char hex_digit(int value) { // 0-9 -> '0'-'9', 10-15 -> 'a'-'f'
  assert(value >= 0 && value < 16);
  return DIGITS[value];
}
static char decimal_digit(int value) {
  assert(value >= 0 && value <= 9);
  return DIGITS[value];
}
static inline OptionU16 lower_hex_to_u16(const char str[4]) {
  // 长度4的字符串，每个都是小写十六进制字符
  OptionU16 r = {0};
  for (int i = 0; i < 4; ++i) {
    if (str[i] >= '0' && str[i] <= '9') {
      r.value += (uint16_t)((str[i] - '0') << ((3 - i) * 4));
    } else if (str[i] >= 'a' && str[i] <= 'f') {
      r.value += (uint16_t)((str[i] - 'a' + 10) << ((3 - i) * 4));
    } else {
      return r; // has_value = 0
    }
  }
  r.has_value = true;
  return r;
}
// 最多7个字符的十进制整数的字符串转为数值
// 如果正常，返回0~9999999，否则返回-1表示错误
// 不支持空白，加减号
// 因为要支持传4MB的数据，所以需要最大长度7
static inline int32_t chars_to_num(const char *s, size_t len) {
  if (len > 7 || len == 0) {
    return -1;
  }
  int32_t value = 0;
  int32_t rank = 1;
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
static inline size_t num_to_four_chars(int value, char s[4]) {
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
