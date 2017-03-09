// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#include "writer.h"
#include "pin64.h"
#include "block.h"

void pin64_writer_t::write(FILE* file, uint32_t data) {
	if (!file)
		return;

	uint8_t temp(data >> 24);
	fwrite(&temp, 1, 1, file);

	temp = (uint8_t)(data >> 16);
	fwrite(&temp, 1, 1, file);

	temp = (uint8_t)(data >> 8);
	fwrite(&temp, 1, 1, file);

	temp = (uint8_t)data;
	fwrite(&temp, 1, 1, file);
}

void pin64_writer_t::write(FILE* file, const uint8_t* data, uint32_t size) {
	if (!file)
		return;

	fwrite(data, 1, size, file);
}

void pin64_writer_t::write(FILE* file, pin64_block_t* block) {
	write(file, pin64_block_t::BLOCK_ID, 4);
	write(file, block->crc32());

	const uint32_t block_size = (uint32_t)block->data()->size();
	write(file, block_size);

	if (block_size > 0)
		write(file, block->data()->bytes(), block_size);
}

void pin64_writer_t::write_data_directory(FILE* file, pin64_t* capture) {
	write(file, pin64_t::BDR_ID, 8);

	std::map<uint32_t, pin64_block_t*>& blocks = capture->blocks();
	write(file, (uint32_t)blocks.size());

	uint32_t offset = static_cast<uint32_t>(capture->header_size() + capture->block_directory_size() + capture->cmdlist_directory_size() + 8);
	for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) {
		write(file, offset);
		offset += static_cast<uint32_t>((block_pair.second)->size());
	}
}

void pin64_writer_t::write_cmdlist_directory(FILE* file, pin64_t* capture) {
	write(file, pin64_t::CDR_ID, 8);

	std::vector<uint32_t>& frames = capture->frames();
	write(file, (uint32_t)frames.size());

	for (uint32_t frame : frames)
		write(file, frame);
}

void pin64_writer_t::write(FILE* file, pin64_t* capture) {
	if (!file || !capture)
		return;

	const uint32_t size_total = static_cast<uint32_t>(capture->size());
	const uint32_t size_header = static_cast<uint32_t>(capture->header_size());
	const uint32_t size_block_dir = static_cast<uint32_t>(capture->block_directory_size());
	const uint32_t size_cmdlist_dir = static_cast<uint32_t>(capture->cmdlist_directory_size());
	const uint32_t size_blocks = static_cast<uint32_t>(capture->blocks_size());
	const uint32_t size_cmdlist = static_cast<uint32_t>(capture->cmdlist_size());

	write(file, pin64_t::CAP_ID, 8);
	write(file, size_total);
	write(file, size_header);
	write(file, size_header + size_block_dir);
	write(file, size_header + size_block_dir + size_cmdlist_dir);
	write(file, size_header + size_block_dir + size_cmdlist_dir + size_blocks);
	write(file, size_header + size_block_dir + size_cmdlist_dir + size_blocks + size_cmdlist);

	write_data_directory(file, capture);
	write_cmdlist_directory(file, capture);

	write(file, pin64_t::BLK_ID, 8);
	for (std::pair<uint32_t, pin64_block_t*> block_pair : capture->blocks())
		write(file, block_pair.second);

	std::vector<uint32_t>& commands = capture->commands();
	write(file, pin64_t::CMD_ID, 8);
	write(file, (uint32_t)commands.size());
	for (uint32_t crc : commands)
		write(file, crc);

	std::vector<uint32_t>& metas = capture->commands();
	write(file, pin64_t::MET_ID, 8);
	write(file, (uint32_t)metas.size());
	for (uint32_t crc : metas)
		write(file, crc);
}


