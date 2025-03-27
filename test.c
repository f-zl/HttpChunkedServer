#include "impl.h"
#include "unity.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void setUp() {}
void tearDown() {}
#define CHECK(value) check(value, #value, strlen(#value))
static void check(int value, const char *s, size_t len) {
  char c[4];
  size_t l = num_to_four_chars(value, c);
  TEST_ASSERT_EQUAL_size_t(len, l);
  TEST_ASSERT_EQUAL_STRING_LEN(s, c, len);
}
static void check1(const char str[4], bool has_value, uint16_t value) {
  OptionU16 u = lower_hex_to_u16(str);
  if (u.has_value) {
    TEST_ASSERT_TRUE(has_value);
    TEST_ASSERT_EQUAL_HEX16(value, u.value);
  } else {
    TEST_ASSERT_FALSE(has_value);
  }
}
static void test_lower_hex_to_u16() {
  check1("1234", 1, 0x1234);
  check1("00fa", 1, 0x00fa);
  check1("fbcd", 1, 0xfbcd);
  check1("0-al", 0, 0);
  check1("0ABF", 0, 0);
}
#define CHECK2(value) check2(#value, value)
static void check2(const char *s, int32_t expect) {
  int32_t actual = chars_to_num(s, strlen(s));
  TEST_ASSERT_EQUAL_INT32(expect, actual);
}
static void test_chars_to_num() {
  CHECK2(124);
  CHECK2(456);
  CHECK2(4194304);
  check2("1918276", 1918276);
  check2("19182761", -1);
  check2("1a", -1);
}
static void test_num_to_chars() {
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
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_num_to_chars);
  RUN_TEST(test_lower_hex_to_u16);
  RUN_TEST(test_chars_to_num);
  return UNITY_END();
}
