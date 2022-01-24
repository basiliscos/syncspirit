#include "flush_file.h"

#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

flush_file_t::flush_file_t(const model::file_info_t& file) noexcept {
    assert(file.is_locally_available());
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    folder_id = folder->get_id();
    device_id = fi->get_device()->device_id().get_sha256();
    file_name = file.get_name();
}

auto flush_file_t::apply_impl(cluster_t &cluster) const noexcept-> outcome::result<void> {
    // NO-OP
    return outcome::success();
}

auto flush_file_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting flush_file_t, folder = {}, file = {}", folder_id, file_name);
    return visitor(*this);
}

