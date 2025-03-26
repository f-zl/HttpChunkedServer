#pragma once
// Node.js Buffer readUInt16LE family
// naming is a bit different: readUInt16LE -> ReadUint16LE
// readFloatLE -> ReadFloat32LE
// the buffer is the first argument, mimic this pointer

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <string.h>

static inline int8_t ReadInt8(const unsigned char b[1]) {
  int8_t i;
  memcpy(&i, b, 1);
  return i;
}
static inline size_t WriteInt8(unsigned char b[1], int8_t i) {
  memcpy(b, &i, 1);
  return 1;
}
static inline uint16_t ReadUint16LE(const unsigned char b[2]) {
  return (uint16_t)((uint16_t)((uint16_t)b[1] << 8) | (uint16_t)b[0]);
}
static inline size_t WriteUint16LE(unsigned char b[2], uint16_t u) {
  b[0] = (uint8_t)u;
  b[1] = (uint8_t)(u >> 8);
  return 2;
}
static inline uint16_t ReadUint16BE(const unsigned char b[2]) {
  return (uint16_t)((uint16_t)((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}
static inline size_t WriteUint16BE(unsigned char b[2], uint16_t u) {
  b[1] = (uint8_t)u;
  b[0] = (uint8_t)(u >> 8);
  return 2;
}
static inline int16_t ReadInt16LE(const unsigned char b[2]) {
  int16_t i;
  uint16_t u = ReadUint16LE(b);
  memcpy(&i, &u, 2);
  return i;
}
static inline size_t WriteInt16LE(unsigned char b[2], int16_t i) {
  uint16_t u;
  memcpy(&u, &i, 2);
  return WriteUint16LE(b, u);
}
static inline int16_t ReadInt16BE(const unsigned char b[2]) {
  int16_t i;
  uint16_t u = ReadUint16BE(b);
  memcpy(&i, &u, 2);
  return i;
}
static inline size_t WriteInt16BE(unsigned char b[2], int16_t i) {
  uint16_t u;
  memcpy(&u, &i, 2);
  return WriteUint16BE(b, u);
}
static inline uint32_t ReadUint32LE(const unsigned char b[4]) {
  return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}
static inline size_t WriteUint32LE(unsigned char b[4], uint32_t u) {
  b[0] = (uint8_t)u;
  b[1] = (uint8_t)(u >> 8);
  b[2] = (uint8_t)(u >> 16);
  b[3] = (uint8_t)(u >> 24);
  return 4;
}
static inline uint32_t ReadUint32BE(const unsigned char b[4]) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline size_t WriteUint32BE(unsigned char b[4], uint32_t u) {
  b[3] = (uint8_t)u;
  b[2] = (uint8_t)(u >> 8);
  b[1] = (uint8_t)(u >> 16);
  b[0] = (uint8_t)(u >> 24);
  return 4;
}
static inline int32_t ReadInt32LE(const unsigned char b[4]) {
  int32_t i;
  uint32_t u = ReadUint32LE(b);
  memcpy(&i, &u, 4);
  return i;
}
static inline size_t WriteInt32LE(unsigned char b[4], int32_t i) {
  uint32_t u;
  memcpy(&u, &i, 4);
  return WriteUint32LE(b, u);
}
static inline int32_t ReadInt32BE(const unsigned char b[4]) {
  int32_t i;
  uint32_t u = ReadUint32BE(b);
  memcpy(&i, &u, 4);
  return i;
}
static inline size_t WriteInt32BE(unsigned char b[4], int32_t i) {
  uint32_t u;
  memcpy(&u, &i, 4);
  return WriteUint32BE(b, u);
}
static inline float ReadFloat32LE(const unsigned char b[4]) {
  uint32_t u = ReadUint32LE(b);
  float f;
  memcpy(&f, &u, 4);
  return f;
}
static inline size_t WriteFloat32LE(unsigned char b[4], float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return WriteUint32LE(b, u);
}
static inline float ReadFloat32BE(const unsigned char b[4]) {
  uint32_t u = ReadUint32BE(b);
  float f;
  memcpy(&f, &u, 4);
  return f;
}
static inline size_t WriteFloat32BE(unsigned char b[4], float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return WriteUint32BE(b, u);
}
static inline uint64_t ReadUint64LE(const unsigned char b[8]) {
  return ((uint64_t)b[7] << 56) | ((uint64_t)b[6] << 48) |
         ((uint64_t)b[5] << 40) | ((uint64_t)b[4] << 32) |
         ((uint64_t)b[3] << 24) | ((uint64_t)b[2] << 16) |
         ((uint64_t)b[1] << 8) | (uint64_t)b[0];
}
static inline size_t WriteUint64LE(unsigned char b[8], uint64_t u) {
  b[0] = (uint8_t)u;
  b[1] = (uint8_t)(u >> 8);
  b[2] = (uint8_t)(u >> 16);
  b[3] = (uint8_t)(u >> 24);
  b[4] = (uint8_t)(u >> 32);
  b[5] = (uint8_t)(u >> 40);
  b[6] = (uint8_t)(u >> 48);
  b[7] = (uint8_t)(u >> 56);
  return 8;
}
static inline uint64_t ReadUint64BE(const unsigned char b[8]) {
  return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
         ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
         ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
         ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}
static inline size_t WriteUint64BE(unsigned char b[8], uint64_t u) {
  b[7] = (uint8_t)u;
  b[6] = (uint8_t)(u >> 8);
  b[5] = (uint8_t)(u >> 16);
  b[4] = (uint8_t)(u >> 24);
  b[3] = (uint8_t)(u >> 32);
  b[2] = (uint8_t)(u >> 40);
  b[1] = (uint8_t)(u >> 48);
  b[0] = (uint8_t)(u >> 56);
  return 8;
}
#ifdef __cplusplus
}
#endif
