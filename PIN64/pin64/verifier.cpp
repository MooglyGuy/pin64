// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#include "verifier.h"
#include "pin64.h"
#include "block.h"

bool pin64_verifier_t::verify(pin64_data_t* data) {
	if (!data) return false;
	if (!verify_headers(data)) return false;
	if (!verify_data_directory(data)) return false;

	std::map<uint32_t, pin64_block_t*> blocks;
	if (!verify_blocks(blocks, data)) {
		for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) delete block_pair.second;
		return false;
	}
	if (!verify_cmdlist_directory(blocks, data)) {
		for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) delete block_pair.second;
		return false;
	}
	if (!verify_cmdlist(blocks, data)) {
		for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) delete block_pair.second;
		return false;
	}
	if (!verify_metas(blocks, data)) {
		for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) delete block_pair.second;
		return false;
	}

	return true;
}

bool pin64_verifier_t::verify_headers(pin64_data_t* data) {
	data->reset();

	if (!verify_preamble(data, 0, pin64_t::CAP_ID, 8)) return false;

	const uint32_t total_size = data->get32();
	if (data->size() != total_size) return false;

	const uint32_t header_size = data->get32();
	if (header_size != (uint32_t)pin64_t::header_size()) return false;

	if (!verify_preamble(data, data->get32(12), pin64_t::BDR_ID, 8)) return false;
	if (!verify_preamble(data, data->get32(16), pin64_t::CDR_ID, 8)) return false;
	if (!verify_preamble(data, data->get32(20), pin64_t::BLK_ID, 8)) return false;
	if (!verify_preamble(data, data->get32(24), pin64_t::CMD_ID, 8)) return false;
	if (!verify_preamble(data, data->get32(28), pin64_t::MET_ID, 8)) return false;

	data->reset();

	return true;
}

bool pin64_verifier_t::verify_preamble(pin64_data_t* data, const uint32_t offset, const uint8_t* compare, const size_t size) {
	data->offset(offset);
	uint8_t temp[256];
	for (size_t i = 0; i < size; i++)
		temp[i] = data->get8();
	return memcmp(temp, compare, size) == 0;
}

bool pin64_verifier_t::verify_data_directory(pin64_data_t* data) {
	const uint32_t data_dir_offset = data->get32(12);
	const uint32_t cmd_dir_offset = data->get32(16);
	const uint32_t data_offset = data->get32(20);
	const uint32_t data_end = data->get32(24) - 1;
	const size_t header_size = cmd_dir_offset - data_dir_offset;

	uint32_t block_count = data->get32(data_dir_offset + 8); // Skip header
	size_t count_size = (block_count + 1) * sizeof(uint32_t) + 8;
	if (count_size != header_size) return false;

	data->offset(data_dir_offset + 12);
	for (uint32_t i = 0; i < block_count; i++) {
		uint32_t block_offset = data->get32();
		if (block_offset < data_offset || block_offset > data_end)
			return false;
	}

	data->reset();

	return true;
}

bool pin64_verifier_t::verify_blocks(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data) {
	const uint32_t cmdlist_start = data->get32(24);
	const uint32_t block_dir_start = data->get32(12);
	const uint32_t block_dir_data_start = block_dir_start + 12;
	const uint32_t block_count = data->get32(block_dir_start + 8); // skip header

	for (uint32_t i = 0; i < block_count; i++) {
		uint32_t block_offset = data->get32(block_dir_data_start + i * 4);
		if (!verify_preamble(data, block_offset, pin64_block_t::BLOCK_ID, 4)) return false;

		uint32_t next_offset = ((i == (block_count - 1)) ? cmdlist_start : data->get32(block_dir_data_start + (i + 1) * 4));
		const size_t calculated_dir_size = next_offset - block_offset;

		data->offset(block_offset + 4); // skip header
		const uint32_t block_crc = data->get32();

		const size_t block_size = data->get32();
		if ((block_size + 12) != calculated_dir_size) return false;

		pin64_block_t *block = new pin64_block_t(data, block_size);
		if (block->crc32() != block_crc) {
			delete block;
			return false;
		}

		blocks[block_crc] = block;
	}

	return true;
}

bool pin64_verifier_t::verify_cmdlist_directory(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data) {
	const uint32_t cmd_count = data->get32(data->get32(24) + 8);

	const uint32_t cmd_dir_start = data->get32(16);

	data->offset(cmd_dir_start + 8); // skip over header
	const uint32_t cmd_dir_count = data->get32();

	for (uint32_t i = 0; i < cmd_dir_count; i++) {
		uint32_t cmd_index = data->get32();
		if (cmd_index >= cmd_count) return false;
	}

	return true;
}

bool pin64_verifier_t::verify_cmdlist(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data) {
	const uint32_t cmd_count = data->get32(data->get32(24) + 8); // skip over header

	data->offset(data->get32(24) + 12); // skip over header + count
	for (uint32_t i = 0; i < cmd_count; i++) {
		uint32_t crc = data->get32();
		if (blocks.find(crc) == blocks.end()) return false;
	}

	return true;
}

bool pin64_verifier_t::verify_metas(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data) {
	const uint32_t meta_count = data->get32(data->get32(28) + 8); // skip over header

	data->offset(data->get32(28) + 12); // skip over header + count
	for (uint32_t i = 0; i < meta_count; i++) {
		uint32_t crc = data->get32();
		if (blocks.find(crc) == blocks.end()) return false;
	}

	return true;
}
