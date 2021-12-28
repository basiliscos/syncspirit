#include "block_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

block_diff_t::block_diff_t(std::string_view folder_id_, std::string_view file_name_, size_t block_index_) noexcept:
    folder_id{folder_id_}, file_name{file_name_}, block_index{block_index_} {

}


auto block_diff_t::visit(block_visitor_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}

