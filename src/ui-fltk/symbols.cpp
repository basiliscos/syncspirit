#include "symbols.h"

#define UTF8_CAST(X) std::string_view(reinterpret_cast<const char *>((X)))

namespace syncspirit::fltk::symbols {

static const auto scanning_raw = u8"⟳\0";
static const auto synchronizing_raw = u8"↓\0";
static const auto online_raw = u8"⚑\0";
static const auto offline_raw = u8"⚐\0";
static const auto connecting_raw = u8"🗲\0";
static const auto discovering_raw = u8"…\0";
static const auto deleted_raw = u8"♻\0";

// ♨
// ♻
// ⚙
// ✆

const std::string_view scaning = UTF8_CAST(scanning_raw);
const std::string_view syncrhonizing = UTF8_CAST(synchronizing_raw);
const std::string_view online = UTF8_CAST(online_raw);
const std::string_view offline = UTF8_CAST(offline_raw);
const std::string_view connecting = UTF8_CAST(connecting_raw);
const std::string_view discovering = UTF8_CAST(discovering_raw);
const std::string_view deleted = UTF8_CAST(deleted_raw);

} // namespace syncspirit::fltk::symbols
