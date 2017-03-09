// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_DATA_H
#define PIN64_DATA_H

#include <cstdint>
#include <vector>

class pin64_data_t {
public:
	pin64_data_t()
		: m_offset(0)
		, m_old_offset(0) {
	}
	virtual ~pin64_data_t() {}

	void reset();
	void clear();
	bool load_file(const char* name);

	// setters
	virtual void put(uint8_t* data, size_t size);
	virtual void put(pin64_data_t* data, size_t size);
	virtual void put8(uint8_t data);
	virtual void put16(uint16_t data);
	virtual void put32(uint32_t data);
	virtual void put64(uint64_t data);
	void offset(size_t offset) { m_offset = offset; }
	void relative_offset(size_t offset) { m_offset += offset; }

	// getters
	virtual uint8_t get8();
	virtual uint8_t get8(size_t offset, bool store_new_offset = false);
	virtual uint16_t get16();
	virtual uint16_t get16(size_t offset, bool store_new_offset = false);
	virtual uint32_t get32();
	virtual uint32_t get32(size_t offset, bool store_new_offset = false);
	virtual uint64_t get64();
	virtual uint64_t get64(size_t offset, bool store_new_offset = false);
	virtual size_t offset() { return m_offset; }
	uint8_t* bytes() { return (m_data.size() > 0) ? &m_data[0] : nullptr; }
	uint8_t* curr(size_t off = 0) { return (m_data.size() >= (m_offset + off)) ? &m_data[m_offset + off] : nullptr; }
	size_t size() const { return m_data.size(); }
	size_t remaining() const { return size() - m_offset; }

private:
	void update_offset(size_t offset, bool store_new_offset = false);

protected:
	std::vector<uint8_t> m_data;

	size_t m_offset;
	size_t m_old_offset;
};

class pin64_dummy_data_t : public pin64_data_t {
public:
	void put(uint8_t* data, size_t size) override {}
	void put(pin64_data_t* data, size_t size) override {}
	void put8(uint8_t data) override {}
	void put16(uint16_t data) override {}
	void put32(uint32_t data) override {}
	void put64(uint64_t data) override {}

	uint8_t get8() override { return 0; }
	uint8_t get8(size_t offset, bool update_current = true) override { return 0; }
	uint16_t get16() override { return 0; }
	uint16_t get16(size_t offset, bool update_current = true) override { return 0; }
	uint32_t get32() override { return 0; }
	uint32_t get32(size_t offset, bool update_current = true) override { return 0; }
	uint64_t get64() override { return 0; }
	uint64_t get64(size_t offset, bool update_current = true) override { return 0; }

	size_t offset() override { return 0; }
};

#endif // PIN64_DATA_H