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
    requests.push_front(&req);
    while (requests.size() && requests.front()->payload.request_payload.block_index == responses.front().block_index) {
        reply_to(*requests.front(), responses.front().data);
        responses.pop_front();
        requests.pop_front();
    }
}

void sample_peer_t::push_response(const std::string& data, size_t index) noexcept {
    if (index == next_block) {
        index = responses.size();
    }
    responses.push_back(response_t{index, data});
}
