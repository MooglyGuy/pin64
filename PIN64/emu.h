#pragma once

#include <cstdio>
#include <cstdint>
#include <stdarg.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

static void fatalerror(const char* msg, ...) {
	va_list v;
	char buf[32768];
	va_start(v, msg);
	vsnprintf(buf, 32767, msg, v);
	va_end(v);

	printf("%s\n", msg);
	*((uint32_t*)0) = 1;
}

// Dummy class
class running_machine {
public:
	running_machine() { }
};

// an rgb15_t is a single combined 15-bit R,G,B value
typedef uint16_t rgb15_t;

// ======================> rgb_t

// an rgb_t is a single combined R,G,B (and optionally alpha) value
class rgb_t {
public:
	// construction/destruction
	constexpr rgb_t() : m_data(0) {}
	constexpr rgb_t(uint32_t data) : m_data(data) {}
	constexpr rgb_t(uint8_t r, uint8_t g, uint8_t b) : m_data((255 << 24) | (r << 16) | (g << 8) | b) {}
	constexpr rgb_t(uint8_t a, uint8_t r, uint8_t g, uint8_t b) : m_data((a << 24) | (r << 16) | (g << 8) | b) {}

	// getters
	constexpr uint8_t a() const { return m_data >> 24; }
	constexpr uint8_t r() const { return m_data >> 16; }
	constexpr uint8_t g() const { return m_data >> 8; }
	constexpr uint8_t b() const { return m_data >> 0; }
	constexpr rgb15_t as_rgb15() const { return ((r() >> 3) << 10) | ((g() >> 3) << 5) | ((b() >> 3) << 0); }
	constexpr uint8_t brightness() const { return (r() * 222 + g() * 707 + b() * 71) / 1000; }
	constexpr uint32_t const *ptr() const { return &m_data; }

	// setters
	rgb_t &set_a(uint8_t a) { m_data &= ~0xff000000; m_data |= a << 24; return *this; }
	rgb_t &set_r(uint8_t r) { m_data &= ~0x00ff0000; m_data |= r << 16; return *this; }
	rgb_t &set_g(uint8_t g) { m_data &= ~0x0000ff00; m_data |= g << 8; return *this; }
	rgb_t &set_b(uint8_t b) { m_data &= ~0x000000ff; m_data |= b << 0; return *this; }

	// implicit conversion operators
	constexpr operator uint32_t() const { return m_data; }

	// operations
	rgb_t &scale8(uint8_t scale) { m_data = rgb_t(clamphi((a() * scale) >> 8), clamphi((r() * scale) >> 8), clamphi((g() * scale) >> 8), clamphi((b() * scale) >> 8)); return *this; }

	// assignment operators
	rgb_t &operator=(uint32_t rhs) { m_data = rhs; return *this; }
	rgb_t &operator+=(const rgb_t &rhs) { m_data = uint32_t(*this + rhs); return *this; }
	rgb_t &operator-=(const rgb_t &rhs) { m_data = uint32_t(*this - rhs); return *this; }

	// arithmetic operators
	constexpr rgb_t operator+(const rgb_t &rhs) const { return rgb_t(clamphi(a() + rhs.a()), clamphi(r() + rhs.r()), clamphi(g() + rhs.g()), clamphi(b() + rhs.b())); }
	constexpr rgb_t operator-(const rgb_t &rhs) const { return rgb_t(clamplo(a() - rhs.a()), clamplo(r() - rhs.r()), clamplo(g() - rhs.g()), clamplo(b() - rhs.b())); }

	// static helpers
	static constexpr uint8_t clamp(int32_t value) { return (value < 0) ? 0 : (value > 255) ? 255 : value; }
	static constexpr uint8_t clamphi(int32_t value) { return (value > 255) ? 255 : value; }
	static constexpr uint8_t clamplo(int32_t value) { return (value < 0) ? 0 : value; }

	// constant factories
	static constexpr rgb_t black() { return rgb_t(0, 0, 0); }
	static constexpr rgb_t white() { return rgb_t(255, 255, 255); }
	static constexpr rgb_t green() { return rgb_t(0, 255, 0); }
	static constexpr rgb_t amber() { return rgb_t(247, 170, 0); }
	static constexpr rgb_t transparent() { return rgb_t(0, 0, 0, 0); }

private:
	uint32_t  m_data;
};

// constants for expression endianness
enum endianness_t {
	ENDIANNESS_LITTLE,
	ENDIANNESS_BIG
};


// declare native endianness to be one or the other
#ifdef LSB_FIRST
const endianness_t ENDIANNESS_NATIVE = ENDIANNESS_LITTLE;
#else
const endianness_t ENDIANNESS_NATIVE = ENDIANNESS_BIG;
#endif

// endian-based value: first value is if 'endian' is little-endian, second is if 'endian' is big-endian
#define ENDIAN_VALUE_LE_BE(endian,leval,beval)  (((endian) == ENDIANNESS_LITTLE) ? (leval) : (beval))

// endian-based value: first value is if native endianness is little-endian, second is if native is big-endian
#define NATIVE_ENDIAN_VALUE_LE_BE(leval,beval)  ENDIAN_VALUE_LE_BE(ENDIANNESS_NATIVE, leval, beval)

// macros for accessing bytes and words within larger chunks

// read/write a byte to a 16-bit space
#define BYTE_XOR_BE(a)                  ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0))
#define BYTE_XOR_LE(a)                  ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,1))

// read/write a byte to a 32-bit space
#define BYTE4_XOR_BE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(3,0))
#define BYTE4_XOR_LE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,3))

// read/write a word to a 32-bit space
#define WORD_XOR_BE(a)                  ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(2,0))
#define WORD_XOR_LE(a)                  ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,2))

// read/write a byte to a 64-bit space
#define BYTE8_XOR_BE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(7,0))
#define BYTE8_XOR_LE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,7))

// read/write a word to a 64-bit space
#define WORD2_XOR_BE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(6,0))
#define WORD2_XOR_LE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,6))

// read/write a dword to a 64-bit space
#define DWORD_XOR_BE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(4,0))
#define DWORD_XOR_LE(a)                 ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,4))
