// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#include "printer.h"
#include "data.h"
#include "pin64.h"
#include "block.h"

void pin64_printer_t::print(pin64_t* capture) {
	printf("Total Size:       %9x bytes\n", (uint32_t)capture->size()); fflush(stdout);
	printf("Header Size:      %9x bytes\n", (uint32_t)capture->header_size()); fflush(stdout);
	printf("Block Dir Size:   %9x bytes\n", (uint32_t)capture->block_directory_size()); fflush(stdout);
	printf("Cmdlist Dir Size: %9x bytes\n", (uint32_t)capture->cmdlist_directory_size()); fflush(stdout);
	printf("Blocks Size:      %9x bytes\n", (uint32_t)capture->blocks_size()); fflush(stdout);
	printf("Cmdlist Size:     %9x bytes\n", (uint32_t)capture->cmdlist_size()); fflush(stdout);
	printf("Metas Size:       %9x bytes\n", (uint32_t)capture->metas_size()); fflush(stdout);

	std::vector<uint32_t>& commands = capture->commands();
	std::map<uint32_t, pin64_block_t*>& blocks = capture->blocks();
	std::vector<uint32_t>& frames = capture->frames();
	std::vector<uint32_t>& metas = capture->metas();

	printf("Command-List Count: %d\n", (uint32_t)frames.size()); fflush(stdout);
	for (size_t i = 0; i < frames.size(); i++) {
		printf("    List %d:\n", (uint32_t)i); fflush(stdout);

		const size_t next_start = ((i == (frames.size() - 1)) ? commands.size() : frames[i + 1]);
		for (uint32_t cmd = frames[i]; cmd < next_start; cmd++) {
			print_command(frames[i], cmd, blocks, commands);
		}

		if (i == (frames.size() - 1)) {
			printf("\n"); fflush(stdout);
		}
	}

	printf("\nData Block Count: %d\n", (uint32_t)blocks.size()); fflush(stdout);
	int i = 0;
	for (std::pair<uint32_t, pin64_block_t*> block_pair : blocks) {
		printf("    Block %d:\n", i); fflush(stdout);

		print_data((block_pair.second));
		if (i == (blocks.size() - 1)) {
			printf("\n"); fflush(stdout);
		}
		i++;
	}
}

void pin64_printer_t::print_data(pin64_block_t* block) {
	pin64_data_t* data = block->data();

	printf("            CRC32: %08x\n", (uint32_t)block->crc32()); fflush(stdout);
	printf("            Data Size: %08x\n", (uint32_t)data->size()); fflush(stdout);
	printf("            Data: "); fflush(stdout);

	const size_t data_size = data->size();
	const size_t row_count = (data_size + 31) / 32;
	const uint8_t* bytes = data->bytes();
	for (uint32_t row = 0; row < row_count; row++) {
		const uint32_t row_index = row * 32;
		const size_t data_remaining = data_size - row_index;
		const size_t col_count = (data_remaining > 32 ? 32 : data_remaining);
		for (size_t col = 0; col < col_count; col++) {
			printf("%02x ", bytes[row_index + col]); fflush(stdout);
		}

		if (row == (row_count - 1)) {
			printf("\n"); fflush(stdout);
		} else {
			printf("\n                  "); fflush(stdout);
		}
	}

	printf("\n"); fflush(stdout);
}

void pin64_printer_t::print_command(uint32_t cmd_start, uint32_t cmd, std::map<uint32_t, pin64_block_t*>& blocks, std::vector<uint32_t>& commands) {
	pin64_block_t* block = blocks[commands[cmd]];
	pin64_data_t* data = block->data();

	printf("        Command %d:\n", cmd - cmd_start); fflush(stdout);
	const uint32_t cmd_size(data->get32());
	printf("        CRC32: %08x\n", (uint32_t)commands[cmd]); fflush(stdout);
	printf("            Packet Data Size: %d words\n", cmd_size); fflush(stdout);
	printf("            Packet Data: "); fflush(stdout);

	bool load_command = false;
	for (uint32_t i = 0; i < cmd_size; i++) {
		const uint64_t cmd_entry(data->get64());
		if (i == 0) {
			const uint8_t top_byte = uint8_t(cmd_entry >> 56) & 0x3f;
			if (top_byte == 0x30 || top_byte == 0x33 || top_byte == 0x34)
				load_command = true;
		}
		printf("%08x%08x\n", uint32_t(cmd_entry >> 32), (uint32_t)cmd_entry); fflush(stdout);

		if (i < (cmd_size - 1))
			printf("                         "); fflush(stdout);
	}

	printf("            Data Block Present: %s\n", load_command ? "Yes" : "No"); fflush(stdout);

	if (load_command) {
		printf("            Data Block CRC32: %08x\n", data->get32()); fflush(stdout);
	}

	data->reset();
};
