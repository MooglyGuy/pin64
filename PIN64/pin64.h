// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_H
#define PIN64_H

#include <cstdio>
#include <vector>
#include <string>
#include <map>

#define PIN64_ENABLE_CAPTURE (0)

#include "emu.h"

class pin64_t;
class pin64_block_t;

class pin64_writer_t {
public:
	static void write(FILE* file, pin64_t* capture);
	static void write(FILE* file, pin64_block_t* capture);
	static void write(FILE* file, uint32_t data);
	static void write(FILE* file, const uint8_t* data, uint32_t size);
};

class pin64_command_t {
public:
	std::vector<uint8_t> data;
};

class pin64_data_t {
public:
	pin64_data_t()
		: m_offset(0)
		, m_old_offset(0) {
	}

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

	void write(FILE* file);

	// getters
	size_t size() const;
	pin64_data_t* data() { return &m_data; }
	uint32_t crc32() const { return m_crc32; }

	static const uint8_t BLOCK_ID[4];

protected:
	uint32_t m_crc32;
	pin64_data_t m_data;
};

class pin64_verifier_t {
public:
	static bool verify(pin64_data_t* data);

private:
	static bool verify_headers(pin64_data_t* data);
	static bool verify_preamble(pin64_data_t* data, const uint32_t offset, const uint8_t* compare, const size_t size);
	static bool verify_data_directory(pin64_data_t* data);
	static bool verify_blocks(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_cmdlist_directory(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_cmdlist(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_metas(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
};

class pin64_printer_t {
public:
	static void print_data(pin64_block_t* block);
	static void print_command(uint32_t cmd_start, uint32_t cmd, std::map<uint32_t, pin64_block_t*>& blocks, std::vector<uint32_t>& commands);
};

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

	void write(FILE* file);

	void update_blocks();
	void update_command();

	bool insert_block(pin64_block_t* block);
	void finish_command();

	void write_data_directory(FILE* file);
	void write_cmdlist_directory(FILE* file);
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