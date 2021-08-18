#include "pair_iterator.h"

using namespace syncspirit::daemon::command;

std::optional<pair_iterator_t::pair_t> pair_iterator_t::next() noexcept {
    auto colon = in.find(":");
    if (colon == in.npos) {
        colon = in.size();
    }

    auto it = in.substr(0, colon);
    auto eq = it.find("=");
    if (eq == it.npos) {
        return {};
    }

    auto f = in.substr(0, eq);
    auto s = in.substr(eq + 1, colon - (eq + 1));
    if (colon < in.size()) {
        in = in.substr(colon + 1);
    } else {
        in = "";
    }
    return pair_t{f, s};
}
