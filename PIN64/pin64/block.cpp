// license:BSD-3-Clause
// copyright-holders:Ryan Holtz

#include "block.h"

#ifdef PIN64_STANDALONE_BUILD
#include <CRC.h>
#endif

const uint8_t pin64_block_t::BLOCK_ID[4] = { 'P', '6', '4', 'B' };

pin64_block_t::pin64_block_t(uint8_t* data, size_t size)
	: pin64_block_t() {
	m_data.put(data, size);
	finalize();
}

pin64_block_t::pin64_block_t(pin64_data_t* data, size_t size)
	: pin64_block_t() {
	m_data.put(data, size);
	finalize();
}

void pin64_block_t::finalize() {
	if (m_data.size() > 0)
#ifdef PIN64_STANDALONE_BUILD
		m_crc32 = CRC::Calculate(m_data.bytes(), m_data.size(), CRC::CRC_32());
#else
		m_crc32 = (uint32_t)util::crc32_creator::simple(m_data.bytes(), (uint32_t)m_data.size());
#endif
	else
		m_crc32 = ~0;
	m_data.reset();
}

void pin64_block_t::clear() {
	m_crc32 = 0;
	m_data.clear();
}

size_t pin64_block_t::size() const {
	return sizeof(uint8_t) * 4  // block header
		+ sizeof(uint32_t)     // data CRC32
		+ sizeof(uint32_t)     // data size
		+ m_data.size();       // data
}
