#include "sample_peer.h"

using namespace syncspirit::test;

void sample_peer_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        log = utils::get_logger("sample.peer");
        p.set_identity("sample.peer", false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&sample_peer_t::on_start_reading);
        p.subscribe_actor(&sample_peer_t::on_block_request);
    });
    if (configure_callback) {
        configure_callback(plugin);
    }
}

void sample_peer_t::on_start_reading(message::start_reading_t &) noexcept { ++start_reading; }

void sample_peer_t::on_block_request(message::block_request_t &req) noexcept {
    requests.push_front(&req);
    log->debug("{}, requesting block # {}", identity, requests.front()->payload.request_payload.block.block_index());
    if (responses.size()) {
        log->debug("{}, top response block # {}", identity, responses.front().block_index);
    }
    auto condition = [&]() -> bool {
        return requests.size() && responses.size() &&
               requests.front()->payload.request_payload.block.block_index() == responses.front().block_index;
    };
    while (condition()) {
        log->debug("{}, matched replying...", identity);
        reply_to(*requests.front(), responses.front().data);
        responses.pop_front();
        requests.pop_front();
    }
}

void sample_peer_t::push_response(const std::string &data, size_t index) noexcept {
    if (index == next_block) {
        index = responses.size();
    }
    responses.push_back(response_t{index, data});
}
