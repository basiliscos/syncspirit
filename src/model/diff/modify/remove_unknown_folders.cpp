#include "remove_unknown_folders.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

auto remove_unknown_folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    if (!keys.empty()) {

        LOG_TRACE(log, "applyging remove_unknown_folders_t, folders = {}", keys.size());
        auto &unknown = cluster.get_unknown_folders();
        for (auto &key : keys) {
            for (auto it = unknown.begin(), prev = unknown.before_begin(); it != unknown.end(); prev = it, ++it) {
                if ((**it).get_key() == key) {
                    unknown.erase_after(prev);
                    break;
                }
            }
        }
    }
    return outcome::success();
}

auto remove_unknown_folders_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_unknown_folders_t");
    return visitor(*this, custom);
}
