// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "LoggingPolicy.h"
#include <spdlog/sinks/sink.h>

namespace Clipboard::Logging
{
	// Also covers dependency logs sent directly to this DLL's default logger.
	// The underlying sink retains its existing formatting and thread safety.
	class Sink final : public spdlog::sinks::sink
	{
	public:
		explicit Sink(spdlog::sink_ptr output) : _output(std::move(output)) {}
		void log(const spdlog::details::log_msg& message) override
		{
			const bool issue = message.level >= spdlog::level::warn && message.level <= spdlog::level::critical;
			if (Allows({ message.payload.data(), message.payload.size() }, message.level == spdlog::level::info, issue)) {
				_output->log(message);
			}
		}
		void flush() override
		{
			// Flush also covers the always-on, bounded build identity records.
			_output->flush();
		}
		void set_pattern(const std::string& pattern) override { _output->set_pattern(pattern); }
		void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override { _output->set_formatter(std::move(formatter)); }
	private:
		spdlog::sink_ptr _output;
	};
}
