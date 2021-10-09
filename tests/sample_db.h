#pragma once

#include <functional>
#include "rotor.hpp"
#include "net/messages.h"
#include "utils/log.h"

namespace syncspirit::test {

namespace r = rotor;
namespace message = syncspirit::net::message;

struct sample_db_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_store_folder_info(message::store_folder_info_request_t &message) noexcept;
    void on_store_file(message::store_file_request_t &message) noexcept;
    void save(model::folder_info_ptr_t &folder_info) noexcept;

    utils::logger_t log;
};

} // namespace syncspirit::test
