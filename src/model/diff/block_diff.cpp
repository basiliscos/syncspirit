#include "block_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

block_diff_t::block_diff_t(const file_info_t &file, size_t block_index_) noexcept
    : file_name{file.get_name()}, block_index{block_index_} {
    auto fi = file.get_folder_info();
    folder_id = fi->get_folder()->get_id();
    device_id = fi->get_device()->device_id().get_sha256();
}

auto block_diff_t::visit(block_visitor_t &) const noexcept -> outcome::result<void> { return outcome::success(); }
