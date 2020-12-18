#pragma once
#include <atomic>

namespace syncspirit {
namespace console {

extern std::atomic_bool shutdown_flag;
extern std::atomic_bool reset_term_flag;

void term_prepare() noexcept;
void term_restore() noexcept;
bool install_signal_handlers() noexcept;

} // namespace console
} // namespace syncspirit
