#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#include <csignal>
#include <spdlog/spdlog.h>
#include "utils.h"

namespace syncspirit::console {

static termios orig_termios;
static bool restore_installed = false;

std::atomic_bool shutdown_flag = false;
std::atomic_bool reset_term_flag = false;

void term_restore() noexcept { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void term_prepare() noexcept {

    if (!restore_installed) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        std::atexit(term_restore);
        restore_installed = true;
    };

    struct termios ts;

    tcgetattr(STDIN_FILENO, &ts);
    ts.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &ts);
}

bool install_signal_handlers() noexcept {
    struct sigaction act;
    act.sa_handler = [](int) { shutdown_flag = true; };
    if (sigaction(SIGINT, &act, nullptr) != 0) {
        spdlog::critical("cannot set signal handler");
        return false;
    }
    act.sa_handler = [](int) { reset_term_flag = true; };
    if (sigaction(SIGCONT, &act, nullptr) != 0) {
        spdlog::critical("cannot set signal handler");
        return false;
    }
    return true;
}

} // namespace syncspirit::console
