#pragma once

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include <Windows.h>
#include <ShlObj_core.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

using namespace std::literals;

// CommonLibF4RD intentionally keeps these helpers in its own namespaces.
// The legacy source used the conventional CommonLib aliases throughout, so
// provide them once in the writable port instead of repeating qualifiers.
namespace logger = F4SE::log;
namespace stl = F4SE::stl;
