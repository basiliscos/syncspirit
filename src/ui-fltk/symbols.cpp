#include "symbols.h"

#define UTF8_CAST(X) std::string_view(reinterpret_cast<const char *>(X), sizeof(X))

namespace syncspirit::fltk::symbols {

static auto scanning_raw = u8"⟳";
static auto synchronizing_raw = u8"↓";
static auto online_raw = u8"⚑";
static auto offline_raw = u8"⚐";
static auto connecting_raw = u8"🗲";
static auto discovering_raw = u8"…";
static auto deleted_raw = u8"♻";

// ♨
// ♻
// ⚙
// ✆

std::string_view scaning = UTF8_CAST(scanning_raw);
std::string_view syncrhonizing = UTF8_CAST(synchronizing_raw);
std::string_view online = UTF8_CAST(online_raw);
std::string_view offline = UTF8_CAST(offline_raw);
std::string_view connecting = UTF8_CAST(connecting_raw);
std::string_view discovering = UTF8_CAST(discovering_raw);
std::string_view deleted = UTF8_CAST(deleted_raw);

} // namespace syncspirit::fltk::symbols
