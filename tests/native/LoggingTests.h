// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "LoggingSink.h"
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ostream_sink.h>
#include <array>
#include <sstream>

template <class Tests>
void CheckLogging(Tests& tests)
{
	using namespace Clipboard::Logging;
	SetEnabled(false);
	std::ostringstream output;
	auto destination = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
	spdlog::logger log("logging-test", std::make_shared<Sink>(destination));
	log.set_level(spdlog::level::trace);
	log.set_formatter(std::make_unique<spdlog::pattern_formatter>("%v", spdlog::pattern_time_type::local, "\n"));
	int evaluated = 0;
	CLIPBOARD_DEBUG_LOG(log.info("expensive diagnostic {}", ++evaluated));
	tests.Expect(evaluated == 0 && output.str().empty(), "disabled diagnostics skip argument evaluation");
	const std::array<std::string_view, 4> allowed{
		"----- Clipboard session start -----",
		"Attempting Runtime Database resolution on AE runtime 1-11-240-0; no executable patch whitelist",
		"F4RD OK runtime=1-11-240-0 mode=patterns source=known_rva",
		"Resolved all 41 reviewed CommonLib Runtime Database symbols"
	};
	std::string expected;
	{
		StartupWindow startupWindow;
		for (auto message : allowed) { log.info("{}", message); expected += std::string(message) + "\n"; }
		for (auto level : { spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
			spdlog::level::warn, spdlog::level::err, spdlog::level::critical }) {
			log.log(level, "ordinary startup/registration/error diagnostic");
			if (level >= spdlog::level::warn) { expected += "ordinary startup/registration/error diagnostic\n"; }
		}
		log.info("F4RD OK mapping runtime=1-11-240-0 total=82 resolved=82");
		log.info("Resolved all 41 required Runtime Database IDs");
		log.info("Resolved all invalid reviewed CommonLib Runtime Database symbols");
		log.warn("{}", allowed[0]);
		expected += std::string(allowed[0]) + "\n";
		tests.Expect(output.str() == expected, "Off retains startup identities and all warning/error severities");
	}
	for (auto message : allowed) { log.info("{}", message); }
	log.error("ordinary runtime error");
	expected += "ordinary runtime error\n";
	tests.Expect(output.str() == expected, "Off suppresses routine details but retains dependency errors after startup");
	for (const auto message : { "Clipboard build identity: dll internal=100 product=3.0.0",
		"Clipboard build identity: script=ClipboardManager internal=100 dll=100 match=true",
		"Clipboard build identity: script=ClipboardQuest internal=99 dll=100 match=false" }) {
		log.info("{}", message);
		expected += std::string(message) + "\n";
	}
	log.debug("Clipboard build identity: not an identity info record");
	log.info("ordinary runtime diagnostic after identity");
	log.flush();
	tests.Expect(output.str() == expected, "Off retains native, script and mismatched identity records without enabling other logs");
	SetEnabled(true);
	for (auto level : { spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
		spdlog::level::warn, spdlog::level::err, spdlog::level::critical }) {
		log.log(level, "enabled diagnostic");
		expected += "enabled diagnostic\n";
	}
	CLIPBOARD_DEBUG_LOG(log.info("expensive diagnostic {}", ++evaluated));
	expected += "expensive diagnostic 1\n";
	tests.Expect(evaluated == 1 && output.str() == expected, "On restores every severity and lazy diagnostic arguments");
	SetEnabled(false);
	log.critical("after turning Off");
	expected += "after turning Off\n";
	CLIPBOARD_DEBUG_LOG(log.info("expensive diagnostic {}", ++evaluated));
	tests.Expect(evaluated == 1 && output.str() == expected, "turning Off retains critical errors while suppressing diagnostic arguments");

	// Exercise the actual lazy gate with a side effect in the formatted argument,
	// including nested imports and the unchanged non-verbose warning route.
	for (bool enabledNow : { false, true }) {
		SetEnabled(enabledNow);
		for (bool verbose : { false, true }) {
			for (unsigned depth : { 0U, 1U, 2U }) {
				int prepared = 0;
				CLIPBOARD_IMPORT_DETAIL_LOG(depth != 0, verbose, log.info("row {}", ++prepared));
				const bool expectDetail = enabledNow && (depth == 0 || verbose);
				tests.Expect(prepared == (expectDetail ? 1 : 0), "routine import text is prepared only when it will be logged");
			}
		}
	}
	output.str("");
	SetEnabled(false);
	log.warn("WARNING: missing endpoint {}", ++evaluated);
	log.error("ERROR: failed operation {}", ++evaluated);
	tests.Expect(output.str() == "WARNING: missing endpoint 2\nERROR: failed operation 3\n", "Off keeps warning/error arguments and output independent of import verbosity");
}
