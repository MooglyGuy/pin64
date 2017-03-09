// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_H
#define PIN64_H

#include "data.h"
#include <vector>
#include <map>

#define PIN64_ENABLE_CAPTURE (0)

#include "../emu.h"

class pin64_t;
class pin64_block_t;
class pin64_data_t;

class pin64_t {
public:
	pin64_t()
		: m_capture_file(nullptr)
		, m_capture_index(~0)
		, m_current_frame(0)
		, m_capture_frames(0)
		, m_commands_left(0)
		, m_command_index(0)
		, m_current_data(nullptr)
		, m_current_command(nullptr)
		, m_current_meta(nullptr)
		, m_playing(false) {
	}
	~pin64_t();

	void start(int frames, uint32_t vi_control, uint32_t vi_origin, uint32_t vi_hstart, uint32_t vi_xscale, uint32_t vi_vstart, uint32_t vi_yscale, uint32_t vi_width);
	void finish();
	void clear();
	void print();

	void mark_frame(running_machine& machine, uint32_t vi_control = 0, uint32_t vi_origin = 0,
		uint32_t vi_hstart = 0, uint32_t vi_xscale = 0, uint32_t vi_vstart = 0, uint32_t vi_yscale = 0, uint32_t vi_width = 0);
	bool load(int index);
	void play(int index);
	bool verify(int index);

	void command(uint64_t* cmd_data, uint32_t size);
	pin64_block_t* command() { return m_current_command; }
	void next_command();

	uint32_t vi_control();
	uint32_t vi_origin();
	uint32_t vi_hstart();
	uint32_t vi_xscale();
	uint32_t vi_vstart();
	uint32_t vi_yscale();
	uint32_t vi_width();

	void data_begin();
	pin64_data_t* data_block();
	pin64_block_t& block() { return *m_current_data; }
	std::map<uint32_t, pin64_block_t*>& blocks() { return m_blocks; }
	std::vector<uint32_t>& commands() { return m_commands; }
	std::vector<uint32_t>& frames() { return m_frames; }
	std::vector<uint32_t>& metas() { return m_metas; }

	void data_end();

	bool capturing() const { return m_capture_file != nullptr; }
	bool playing() const { return m_playing; }
	uint32_t commands_left() const { return m_commands_left; }

	size_t size();
	size_t block_directory_size() const;
	size_t cmdlist_directory_size() const;
	size_t blocks_size();
	size_t cmdlist_size() const;
	size_t metas_size() const;
	static size_t header_size();

	static const uint8_t CAP_ID[8];
	static const uint8_t BDR_ID[8];
	static const uint8_t CDR_ID[8];
	static const uint8_t BLK_ID[8];
	static const uint8_t CMD_ID[8];
	static const uint8_t MET_ID[8];

private:
	void add_meta(uint32_t vi_control, uint32_t vi_origin, uint32_t vi_hstart, uint32_t vi_xscale, uint32_t vi_vstart, uint32_t vi_yscale, uint32_t vi_width);

	void update_blocks();
	void update_command();

	bool insert_block(pin64_block_t* block);
	void finish_command();

	void init_capture_index();

	void finalize();

	FILE *m_capture_file;
	uint32_t m_capture_index;
	uint32_t m_current_frame;
	uint32_t m_capture_frames;

	uint32_t m_commands_left;
	uint32_t m_command_index;

	pin64_block_t* m_current_data;
	pin64_block_t* m_current_command;
	pin64_block_t* m_current_meta;
	std::map<uint32_t, pin64_block_t*> m_blocks;

	std::vector<uint32_t> m_commands;
	std::vector<uint32_t> m_frames;
	std::vector<uint32_t> m_metas;

	bool m_playing;

	pin64_dummy_data_t m_dummy_data;
};

#endif // PIN64_H