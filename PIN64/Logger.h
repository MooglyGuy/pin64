#pragma once

#include <cstdio>
#include <string>

class Logger {
public:
	~Logger();

	static void Log(const char* message, ...);
	static void StartLogging(const std::string& fileName);
	static void StopLogging();

private:
	void _Log(const char* message);
	Logger(const std::string& fileName);

	static Logger* mInstance;
	FILE* mLogFile;
};
