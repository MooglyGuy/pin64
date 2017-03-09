// license:BSD-3-Clause
// copyright-holders:Ryan Holtz

#include "pin64.h"
#include "block.h"
#include "writer.h"
#include "verifier.h"

#define CAP_NAME "pin64_%d.cap"

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

	pin64_writer_t::write(m_capture_file, this);

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
#ifdef PIN64_STANDALONE_BUILD
	if (m_commands_left == 0) {
		mark_frame(running_machine());
	}
#endif
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
