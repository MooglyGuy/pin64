#include "Logger.h"
#include <stdarg.h>

Logger* Logger::mInstance = nullptr;

Logger::Logger(const std::string& fileName)
	: mLogFile(nullptr) {
	mLogFile = fopen(fileName.c_str(), "w");
	if (!mLogFile) {
		fprintf(stderr, "Error: Unable to open file %s for writing.\n", fileName.c_str());
	}
}

void Logger::Log(const char* message, ...) {
	va_list v;
	char buf[32768];
	va_start(v, message);
	vsnprintf(buf, 32767, message, v);
	va_end(v);

	mInstance->_Log(buf);
}

void Logger::_Log(const char* message) {
	if (!mLogFile) return;

	fprintf(mLogFile, message);
}

Logger::~Logger() {
	if (mLogFile)
		fclose(mLogFile);
}

void Logger::StartLogging(const std::string& fileName) {
	mInstance = new Logger(fileName);
}

void Logger::StopLogging() {
	delete mInstance;
}