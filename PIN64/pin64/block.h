// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_BLOCK_H
#define PIN64_BLOCK_H

#include <cstdint>
#include "data.h"

class pin64_block_t {
public:
	pin64_block_t()
		: m_crc32{ 0 } {
	}
	pin64_block_t(uint8_t* data, size_t size);
	pin64_block_t(pin64_data_t* data, size_t size);
	virtual ~pin64_block_t() {}

	void finalize();
	void clear();

	// getters
	size_t size() const;
	pin64_data_t* data() { return &m_data; }
	uint32_t crc32() const { return m_crc32; }

	static const uint8_t BLOCK_ID[4];

protected:
	uint32_t m_crc32;
	pin64_data_t m_data;
};

#endif // PIN64_BLOCK_H
