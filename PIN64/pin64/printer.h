// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_PRINTER_H
#define PIN64_PRINTER_H

#include <cstdint>
#include <map>
#include <vector>

class pin64_t;
class pin64_block_t;

class pin64_printer_t {
public:
	static void print(pin64_t* capture);
	static void print_data(pin64_block_t* block);
	static void print_command(uint32_t cmd_start, uint32_t cmd, std::map<uint32_t, pin64_block_t*>& blocks, std::vector<uint32_t>& commands);
};

#endif // PIN64_PRINTER_H
