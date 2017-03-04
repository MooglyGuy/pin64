// license:BSD-3-Clause
// copyright-holders:Ryan Holtz

#include "pin64.h"

#define CAP_NAME "pin64_%d.cap"

#ifdef PIN64_STANDALONE_BUILD
#include <CRC.h>
#endif

// pin64_writer_t members

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
	if (block != nullptr) {
		
	}
	pin64_writer_t::write(file, pin64_block_t::BLOCK_ID, 4);
	pin64_writer_t::write(file, block->crc32());
	pin64_writer_t::write(file, (uint32_t)block->data()->size());
	if (m_data.size() > 0)
		pin64_writer_t::write(file, m_data.bytes(), (uint32_t)m_data.size());
}

void pin64_writer_t::write(FILE* file, pin64_t* capture) {
	if (!file || !capture)
		return;

	const size_t size_total = capture->size();
	const size_t size_header = capture->header_size();
	const size_t size_block_dir = capture->block_directory_size();
	const size_t size_cmdlist_dir = capture->cmdlist_directory_size();
	const size_t size_blocks = capture->blocks_size();
	const size_t size_cmdlist = capture->cmdlist_size();

	pin64_writer_t::write(file, pin64_t::CAP_ID, 8);
	pin64_writer_t::write(file, (uint32_t)size_total);
	pin64_writer_t::write(file, (uint32_t)size_header);
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir + size_blocks));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir + size_blocks + size_cmdlist));

	write_data_directory(file);
	write_cmdlist_directory(file);

	pin64_writer_t::write(file, pin64_t::BLK_ID, 8);
	for (std::pair<uint32_t, pin64_block_t*> block_pair : capture->blocks())
		(block_pair.second)->write(file);

	pin64_writer_t::write(file, pin64_t::CMD_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_commands.size());
	for (uint32_t crc : m_commands)
		pin64_writer_t::write(file, crc);

	pin64_writer_t::write(file, pin64_t::MET_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_metas.size());
	for (uint32_t crc : m_metas)
		pin64_writer_t::write(file, crc);
}


// pin64_data_t members

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



// pin64_printer_t members

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



// pin64_block_t members

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



// pin64_verifier_t members

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



// pin64_t members

const uint8_t pin64_t::CAP_ID[8] = { 'P', 'I', 'N', '6', '4', 'C', 'A', 'P' };
const uint8_t pin64_t::BDR_ID[8] = { 'P', 'I', 'N', '6', '4', 'B', 'D', 'R' };
const uint8_t pin64_t::CDR_ID[8] = { 'P', 'I', 'N', '6', '4', 'C', 'D', 'R' };
const uint8_t pin64_t::BLK_ID[8] = { 'P', 'I', 'N', '6', '4', 'B', 'L', 'K' };
const uint8_t pin64_t::CMD_ID[8] = { 'P', 'I', 'N', '6', '4', 'C', 'M', 'D' };
const uint8_t pin64_t::MET_ID[8] = { 'P', 'I', 'N', '6', '4', 'M', 'E', 'T' };

enum pin64_meta_offset : uint32_t {
	VI_CONTROL = 0,
	VI_ORIGIN = 4,
	VI_HSTART = 8,
	VI_XSCALE = 12,
	VI_VSTART = 16,
	VI_YSCALE = 20,
	VI_WIDTH = 24
};

uint32_t pin64_t::vi_control() { return (m_playing ? m_current_meta->data()->get32(VI_CONTROL) : 0); }
uint32_t pin64_t::vi_origin() { return (m_playing ? m_current_meta->data()->get32(VI_ORIGIN) : 0); }
uint32_t pin64_t::vi_hstart() { return (m_playing ? m_current_meta->data()->get32(VI_HSTART) : 0); }
uint32_t pin64_t::vi_xscale() { return (m_playing ? m_current_meta->data()->get32(VI_XSCALE) : 0); }
uint32_t pin64_t::vi_vstart() { return (m_playing ? m_current_meta->data()->get32(VI_VSTART) : 0); }
uint32_t pin64_t::vi_yscale() { return (m_playing ? m_current_meta->data()->get32(VI_YSCALE) : 0); }
uint32_t pin64_t::vi_width() { return (m_playing ? m_current_meta->data()->get32(VI_WIDTH) : 0); }

pin64_t::~pin64_t() {
	if (capturing())
		finish();

	clear();
}

void pin64_t::start(int frames, uint32_t vi_control, uint32_t vi_origin, uint32_t vi_hstart, uint32_t vi_xscale, uint32_t vi_vstart, uint32_t vi_yscale, uint32_t vi_width) {
#if PIN64_ENABLE_CAPTURE
	if (m_capture_index == ~0)
		init_capture_index();

	if (capturing())
		fatalerror("PIN64: Call to start() while already capturing\n");

	char name_buf[256];
	sprintf(name_buf, CAP_NAME, m_capture_index);
	m_capture_index++;

	m_capture_file = fopen(name_buf, "wb");

	m_capture_frames = frames;

	m_frames.push_back(0);
	add_meta(vi_control, vi_origin, vi_hstart, vi_xscale, vi_vstart, vi_yscale, vi_width);
	m_current_frame = 0;
#endif
}

void pin64_t::finish() {
	if (!capturing())
		return;

	finalize();

	write(m_capture_file);

	clear();

	if (!verify(m_capture_index - 1))
		printf("Warning: Captured file did not pass verification.\n");
}

void pin64_t::finalize() {
	finish_command();
	data_end();
}

bool pin64_t::load(int index) {
	if (capturing())
		return false;

	char name_buf[256];
	sprintf(name_buf, CAP_NAME, index);

	pin64_data_t data;
	if (!data.load_file(name_buf))
		return false;

	if (!pin64_verifier_t::verify(&data))
		return false;

	clear();

	const uint32_t block_dir_start = data.get32(12);
	const uint32_t block_dir_data_start = block_dir_start + 12;
	const uint32_t block_count = data.get32(block_dir_start + 8); // skip header

	for (uint32_t i = 0; i < block_count; i++) {
		uint32_t block_offset = data.get32(block_dir_data_start + i * 4);
		data.offset(block_offset + 8); // skip header + CRC
		const size_t block_size = data.get32();

		pin64_block_t *block = new pin64_block_t(&data, block_size);

		m_blocks[block->crc32()] = block;
	}

	data.offset(data.get32(16) + 8); // skip header
	const uint32_t frame_count = data.get32();
	for (uint32_t i = 0; i < frame_count; i++)
		m_frames.push_back(data.get32());

	data.offset(data.get32(24) + 8); // skip header
	const uint32_t command_count = data.get32();
	for (uint32_t i = 0; i < command_count; i++)
		m_commands.push_back(data.get32());

	data.offset(data.get32(28) + 8); // skip header
	const uint32_t meta_count = data.get32();
	for (uint32_t i = 0; i < meta_count; i++)
		m_metas.push_back(data.get32());

	return true;
}

void pin64_t::play(int index) {
	if (capturing() || m_playing)
		return;

	if (!load(index))
		return;

	m_current_frame = 0;

	update_blocks();

	m_playing = true;
}

void pin64_t::update_blocks() {
	m_current_meta = m_blocks[m_metas[m_current_frame]];
	m_command_index = m_frames[m_current_frame];
	m_commands_left = ((m_current_frame == ((uint32_t)m_frames.size() - 1)) ? (uint32_t)m_commands.size() : m_frames[m_current_frame + 1]) - m_command_index;
	update_command();
}

void pin64_t::update_command() {
	m_current_command = ((m_commands_left > 0) ? m_blocks[m_commands[m_command_index]] : nullptr);

	if (m_current_command) {
		pin64_data_t* data = m_current_command->data();
		const uint8_t command_byte = data->get8(4) & 0x3f;
		if (command_byte == 0x30 || command_byte == 0x33 || command_byte == 0x34) {
			m_current_data = m_blocks[data->get32(data->get32(0) * 8 + 4)];
		} else {
			m_current_data = nullptr;
		}
	} else {
		m_current_data = nullptr;
	}
}

void pin64_t::add_meta(uint32_t vi_control, uint32_t vi_origin, uint32_t vi_hstart, uint32_t vi_xscale, uint32_t vi_vstart, uint32_t vi_yscale, uint32_t vi_width) {
	pin64_block_t* info = new pin64_block_t();
	info->data()->put32(vi_control);
	info->data()->put32(vi_origin);
	info->data()->put32(vi_hstart);
	info->data()->put32(vi_xscale);
	info->data()->put32(vi_vstart);
	info->data()->put32(vi_yscale);
	info->data()->put32(vi_width);

	bool inserted = insert_block(info);

	m_metas.push_back(info->crc32());

	if (!inserted)
		delete info;
}

void pin64_t::mark_frame(running_machine& machine, uint32_t vi_control, uint32_t vi_origin, uint32_t vi_hstart, uint32_t vi_xscale, uint32_t vi_vstart, uint32_t vi_yscale, uint32_t vi_width) {
	if (m_playing) {
		m_current_frame++;
		if (m_current_frame >= m_frames.size()) {
			m_playing = false;
		} else {
			update_blocks();
		}
	}
#if PIN64_ENABLE_CAPTURE
	else if (capturing()) {
		if ((m_current_frame + 1) == m_capture_frames && m_capture_frames > 0) {
			finish();
			machine.popmessage("Done recording.");
		} else {
			m_frames.push_back((uint32_t)m_commands.size());
			add_meta(vi_control, vi_origin, vi_hstart, vi_xscale, vi_vstart, vi_yscale, vi_width);
			m_current_frame++;
		}
	}

	if (machine.input().code_pressed_once(KEYCODE_B) && !m_playing && !capturing()) {
		play(0);
	} else if (machine.input().code_pressed_once(KEYCODE_N) && !m_playing && !capturing()) {
		start(1, vi_control, vi_origin, vi_hstart, vi_xscale, vi_vstart, vi_yscale, vi_width);
		machine.popmessage("Capturing PIN64 snapshot to pin64_%d.cap", m_capture_index - 1);
	} else if (machine.input().code_pressed_once(KEYCODE_M) && !m_playing) {
		if (capturing()) {
			finish();
			machine.popmessage("Done recording.");
		} else {
			start(0, vi_control, vi_origin, vi_hstart, vi_xscale, vi_vstart, vi_yscale, vi_width);
			machine.popmessage("Recording PIN64 movie to pin64_%d.cap", m_capture_index - 1);
		}
	}
#endif
}

void pin64_t::next_command() {
	m_commands_left--;
	m_command_index++;

	update_command();
}

void pin64_t::command(uint64_t* cmd_data, uint32_t size) {
	if (!capturing())
		return;

	finish_command();

	m_current_command = new pin64_block_t();
	m_current_command->data()->put32(size);

	printf("c");
	for (uint32_t i = 0; i < size; i++) {
		m_current_command->data()->put64(cmd_data[i]);
		printf(" %08x%08x", (uint32_t)(cmd_data[i] >> 32), (uint32_t)cmd_data[i]);
	}
	printf("\n");
}

void pin64_t::finish_command() {
	if (!m_current_command)
		return;

	bool inserted = insert_block(m_current_command);

	m_commands.push_back(m_current_command->crc32());

	if (!inserted) {
		delete m_current_command;
	}

	m_current_command = nullptr;
}

void pin64_t::data_begin() {
	if (!capturing())
		return;

	if (m_current_data)
		data_end();

	m_current_data = new pin64_block_t();
}

pin64_data_t* pin64_t::data_block() {
	if (!m_current_data)
		return &m_dummy_data;

	return m_current_data->data();
}

bool pin64_t::insert_block(pin64_block_t* block) {
	block->finalize();
	if (m_blocks.find(block->crc32()) == m_blocks.end()) {
		m_blocks[block->crc32()] = block;
		return true;
	}
	return false;
}

void pin64_t::data_end() {
	if (!capturing() || !m_current_data)
		return;

	bool inserted = insert_block(m_current_data);

	m_current_command->data()->put32(m_current_data->crc32());
	finish_command();

	if (!inserted)
		delete m_current_data;

	m_current_data = nullptr;
}

size_t pin64_t::size() {
	return header_size() + block_directory_size() + cmdlist_directory_size() + cmdlist_size() + blocks_size() + metas_size();
}

size_t pin64_t::header_size() {
	return sizeof(char) * 8 // "PIN64CAP"
		+ sizeof(uint32_t) // total file size
		+ sizeof(uint32_t) // start of block directory data
		+ sizeof(uint32_t) // start of command-list directory data
		+ sizeof(uint32_t) // start of blocks
		+ sizeof(uint32_t) // start of commands
		+ sizeof(uint32_t);// start of metas
}

size_t pin64_t::block_directory_size() const {
	return (m_blocks.size() + 1) * sizeof(uint32_t) + +sizeof(char) * 8;
}

size_t pin64_t::cmdlist_directory_size() const {
	return (m_frames.size() + 1) * sizeof(uint32_t) + +sizeof(char) * 8;
}

size_t pin64_t::blocks_size() {
	size_t block_size = sizeof(char) * 8;
	for (std::pair<uint32_t, pin64_block_t*> block_pair : m_blocks)
		block_size += (block_pair.second)->size();

	return block_size;
}

size_t pin64_t::cmdlist_size() const {
	return (m_commands.size() + 1) * sizeof(uint32_t) + sizeof(char) * 8;
}

size_t pin64_t::metas_size() const {
	return (m_metas.size() + 1) * sizeof(uint32_t) + sizeof(char) * 8;
}

void pin64_t::print() {
	printf("Total Size:       %9x bytes\n", (uint32_t)size()); fflush(stdout);
	printf("Header Size:      %9x bytes\n", (uint32_t)header_size()); fflush(stdout);
	printf("Block Dir Size:   %9x bytes\n", (uint32_t)block_directory_size()); fflush(stdout);
	printf("Cmdlist Dir Size: %9x bytes\n", (uint32_t)cmdlist_directory_size()); fflush(stdout);
	printf("Blocks Size:      %9x bytes\n", (uint32_t)blocks_size()); fflush(stdout);
	printf("Cmdlist Size:     %9x bytes\n", (uint32_t)cmdlist_size()); fflush(stdout);
	printf("Metas Size:       %9x bytes\n", (uint32_t)metas_size()); fflush(stdout);

	printf("Command-List Count: %d\n", (uint32_t)m_frames.size()); fflush(stdout);
	for (size_t i = 0; i < m_frames.size(); i++) {
		printf("    List %d:\n", (uint32_t)i); fflush(stdout);

		const size_t next_start = ((i == (m_frames.size() - 1)) ? m_commands.size() : m_frames[i + 1]);
		for (uint32_t cmd = m_frames[i]; cmd < next_start; cmd++) {
			pin64_printer_t::print_command(m_frames[i], cmd, m_blocks, m_commands);
		}
		if (i == (m_frames.size() - 1)) {
			printf("\n"); fflush(stdout);
		}
	}

	printf("\nData Block Count: %d\n", (uint32_t)m_blocks.size()); fflush(stdout);
	int i = 0;
	for (std::pair<uint32_t, pin64_block_t*> block_pair : m_blocks) {
		printf("    Block %d:\n", i); fflush(stdout);

		pin64_printer_t::print_data((block_pair.second));
		if (i == (m_blocks.size() - 1)) {
			printf("\n"); fflush(stdout);
		}
		i++;
	}
}

void pin64_t::write(FILE* file) {
	const size_t size_total = size();
	const size_t size_header = header_size();
	const size_t size_block_dir = block_directory_size();
	const size_t size_cmdlist_dir = cmdlist_directory_size();
	const size_t size_blocks = blocks_size();
	const size_t size_cmdlist = cmdlist_size();

	pin64_writer_t::write(file, CAP_ID, 8);
	pin64_writer_t::write(file, (uint32_t)size_total);
	pin64_writer_t::write(file, (uint32_t)size_header);
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir + size_blocks));
	pin64_writer_t::write(file, uint32_t(size_header + size_block_dir + size_cmdlist_dir + size_blocks + size_cmdlist));

	write_data_directory(file);
	write_cmdlist_directory(file);

	pin64_writer_t::write(file, BLK_ID, 8);
	for (std::pair<uint32_t, pin64_block_t*> block_pair : m_blocks)
		(block_pair.second)->write(file);

	pin64_writer_t::write(file, CMD_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_commands.size());
	for (uint32_t crc : m_commands)
		pin64_writer_t::write(file, crc);

	pin64_writer_t::write(file, MET_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_metas.size());
	for (uint32_t crc : m_metas)
		pin64_writer_t::write(file, crc);
}

void pin64_t::write_data_directory(FILE* file) {
	pin64_writer_t::write(file, BDR_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_blocks.size());
	size_t offset(header_size() + block_directory_size() + cmdlist_directory_size() + 8);
	for (std::pair<uint32_t, pin64_block_t*> block_pair : m_blocks) {
		pin64_writer_t::write(file, (uint32_t)offset);
		offset += (block_pair.second)->size();
	}
}

void pin64_t::write_cmdlist_directory(FILE* file) {
	pin64_writer_t::write(file, CDR_ID, 8);
	pin64_writer_t::write(file, (uint32_t)m_frames.size());
	for (uint32_t frame : m_frames)
		pin64_writer_t::write(file, frame);
}

bool pin64_t::verify(int index) {
	char name_buf[256];
	sprintf(name_buf, CAP_NAME, index);

	pin64_data_t data;
	if (!data.load_file(name_buf))
		return false;

	return pin64_verifier_t::verify(&data);
}

void pin64_t::clear() {
	if (capturing()) {
		fclose(m_capture_file);
		m_capture_file = nullptr;
	}

	for (std::pair<uint32_t, pin64_block_t*> block_pair : m_blocks)
		delete block_pair.second;

	m_current_frame = 0;
	m_commands_left = 0;
	m_command_index = 0;

	m_blocks.clear();
	m_commands.clear();
	m_frames.clear();
	m_metas.clear();

	m_current_data = nullptr;
	m_current_command = nullptr;
	m_current_meta = nullptr;
}

void pin64_t::init_capture_index() {
	char name_buf[256];
	bool found = true;

	m_capture_index = 0;

	do {
		sprintf(name_buf, CAP_NAME, m_capture_index);

		FILE* temp = fopen(name_buf, "rb");
		if (temp == nullptr) {
			break;
		} else {
			fclose(temp);
			m_capture_index++;
		}
	} while (found);
}
