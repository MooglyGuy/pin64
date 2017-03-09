// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_WRITER_H
#define PIN64_WRITER_H

#include <cstdio>
#include <cstdint>

class pin64_t;
class pin64_block_t;

class pin64_writer_t {
public:
	static void write(FILE* file, pin64_t* capture);
	static void write(FILE* file, pin64_block_t* capture);
	static void write(FILE* file, uint32_t data);
	static void write(FILE* file, const uint8_t* data, uint32_t size);

private:
	static void write_data_directory(FILE* file, pin64_t* capture);
	static void write_cmdlist_directory(FILE* file, pin64_t* capture);
};

#endif // PIN64_WRITER_H