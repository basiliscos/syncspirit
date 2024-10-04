#include "messages.h"

using namespace syncspirit::net::payload;

block_request_t::block_request_t(const model::file_info_ptr_t &file_, const model::file_block_t &block_) noexcept
    : file{file_}, block{block_} {}
