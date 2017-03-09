// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#include "data.h"
#include "../emu.h"

bool pin64_data_t::load_file(const char* name) {
	FILE* file = fopen(name, "rb");

	if (file == nullptr) {
		printf("Unable to load file %s for verification.\n", name);
		return false;
	}

	fseek(file, 0, SEEK_END);
	uint32_t file_size = (uint32_t)ftell(file);
	fseek(file, 0, SEEK_SET);

	uint8_t* data = new uint8_t[file_size];
	fread(data, 1, file_size, file);
	m_data.assign(data, data + file_size);
	delete[] data;

	fclose(file);

	return true;
}

void pin64_data_t::put(uint8_t* data, size_t size) {
	m_data.insert(m_data.end(), data, data + size);
	m_offset += size;
}

void pin64_data_t::put(pin64_data_t* data, size_t size) {
	m_data.insert(m_data.end(), data->curr(), data->curr(size));
	m_offset += size;
}

void pin64_data_t::put8(uint8_t data) {
	m_data.push_back(data);
	m_offset++;
}

void pin64_data_t::put16(uint16_t data) {
	put8((uint8_t)(data >> 8));
	put8((uint8_t)data);
}

void pin64_data_t::put32(uint32_t data) {
	put16((uint16_t)(data >> 16));
	put16((uint16_t)data);
}

void pin64_data_t::put64(uint64_t data) {
	put32((uint32_t)(data >> 32));
	put32((uint32_t)data);
}

uint8_t pin64_data_t::get8() {
	if (m_offset >= m_data.size())
		fatalerror("PIN64: Call to pin64_data_t::get8() at end of block (requested offset %x, size %x)\n", (uint32_t)m_offset, (uint32_t)m_data.size());

	uint8_t ret = m_data[m_offset];
	m_offset++;

	return ret;
}

uint16_t pin64_data_t::get16() {
	uint16_t ret = (uint16_t)get8() << 8;
	return ret | get8();
}

uint32_t pin64_data_t::get32() {
	uint32_t ret = (uint32_t)get16() << 16;
	return ret | get16();
}

uint64_t pin64_data_t::get64() {
	uint64_t ret = (uint64_t)get32() << 32;
	return ret | get32();
}

uint8_t pin64_data_t::get8(size_t offset, bool store_new_offset) {
	update_offset(offset, store_new_offset);

	uint8_t ret = get8();
	m_offset = m_old_offset;
	return ret;
}

uint16_t pin64_data_t::get16(size_t offset, bool store_new_offset) {
	update_offset(offset, store_new_offset);

	uint16_t ret = get16();
	m_offset = m_old_offset;
	return ret;
}

uint32_t pin64_data_t::get32(size_t offset, bool store_new_offset) {
	update_offset(offset, store_new_offset);

	uint32_t ret = get32();
	m_offset = m_old_offset;
	return ret;
}

uint64_t pin64_data_t::get64(size_t offset, bool store_new_offset) {
	update_offset(offset, store_new_offset);

	uint64_t ret = get64();
	m_offset = m_old_offset;
	return ret;
}

void pin64_data_t::reset() {
	m_old_offset = 0;
	m_offset = 0;
}

void pin64_data_t::clear() {
	reset();
	m_data.clear();
}

void pin64_data_t::update_offset(size_t offset, bool store_new_offset) {
	m_old_offset = (store_new_offset ? offset : m_offset);
	m_offset = offset;
}
