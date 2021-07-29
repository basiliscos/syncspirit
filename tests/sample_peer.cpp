#include "sample_peer.h"

using namespace syncspirit::test;

void sample_peer_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&sample_peer_t::on_start_reading);
        p.subscribe_actor(&sample_peer_t::on_block_request);
    });
    if (configure_callback) {
        configure_callback(plugin);
    }
}

void sample_peer_t::on_start_reading(message::start_reading_t &) noexcept {
    ++start_reading;
}

void sample_peer_t::on_block_request(message::block_request_t &req) noexcept {
    assert(responses.size());
    reply_to(req, responses.front());
    responses.pop_front();
}
