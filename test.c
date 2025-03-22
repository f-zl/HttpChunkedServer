#include <assert.h>
#include <stddef.h>
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
  puts("Done");
}
