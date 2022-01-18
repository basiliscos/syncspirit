#include "scan_actor.h"
#include "model/diff/modify/file_availability.h"
#include "model/diff/modify/blocks_availability.h"
#include "net/names.h"
#include "utils/error_code.h"
#include "utils/tls.h"
#include "utils.h"
#include <fstream>
#include <algorithm>

namespace sys = boost::system;
using namespace syncspirit::fs;

template<class> inline constexpr bool always_false_v = false;

scan_actor_t::scan_actor_t(config_t &cfg) : r::actor_base_t{cfg},  cluster{cfg.cluster},
    fs_config{cfg.fs_config}, hasher_proxy{cfg.hasher_proxy}, requested_hashes_limit{cfg.requested_hashes_limit} {
    log = utils::get_logger("scan::actor");
}

void scan_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>(
        [&](auto &p) { p.set_identity("fs::scan_actor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>( [&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&scan_actor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(hasher_proxy, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&scan_actor_t::on_scan);
        p.subscribe_actor(&scan_actor_t::on_initiate_scan);
        p.subscribe_actor(&scan_actor_t::on_hash);
        p.subscribe_actor(&scan_actor_t::on_rehash);
    });
}

void scan_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    for(auto it : cluster->get_folders()) {
        send<payload::scan_folder_t>(address, std::string(it.item->get_id()));
    }
    r::actor_base_t::on_start();
}

void scan_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void scan_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    if (message.payload.custom != this) {
        ++generation;
    }
}

void scan_actor_t::initiate_scan(std::string_view folder_id) noexcept {
    LOG_DEBUG(log, "{}, initiating scan of {}", identity, folder_id);
    ++generation;
    auto task = scan_task_ptr_t(new scan_task_t(cluster, folder_id, fs_config));
    send<payload::scan_progress_t>(address, std::move(task), generation);
}


void scan_actor_t::on_initiate_scan(message::scan_folder_t &message) noexcept {
    if (!generation) {
        initiate_scan(message.payload.folder_id);
    } else {
        queue.emplace_back(&message);
    }
}

void scan_actor_t::process_queue() noexcept {
    if (!queue.empty() && state == r::state_t::OPERATIONAL) {
        auto& msg = queue.front();
        initiate_scan(msg->payload.folder_id);
        queue.pop_front();
    }
}


void scan_actor_t::on_scan(message::scan_progress_t &message) noexcept {
    auto gen = message.payload.generation;
    auto& task = message.payload.task;
    auto folder_id = task->get_folder_id();
    LOG_TRACE(log, "{}, on_scan, folder = {}", identity, folder_id);
    if (gen != generation) {
        LOG_TRACE(log, "{}, outdated generation ({} vs {}), will renew scan_task", identity, gen, generation);
        auto new_task = scan_task_ptr_t(new scan_task_t(cluster, folder_id, fs_config));
        send<payload::scan_progress_t>(address, std::move(new_task), generation);
        return;
    }

    auto r = task->advance();
    bool stop_processing = false;
    bool completed = false;
    std::visit([&](auto&& r){
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, bool>) {
            stop_processing = !r;
            if (stop_processing) {
                completed = true;
            }
        }
        else if constexpr (std::is_same_v<T, scan_errors_t>) {
            send<model::payload::io_error_t>(coordinator, std::move(r));
        }
        else if constexpr (std::is_same_v<T, unchanged_meta_t>) {
            auto diff = model::diff::cluster_diff_ptr_t{};
            diff = new model::diff::modify::file_availability_t(r.file);
            send<model::payload::model_update_t>(coordinator, std::move(diff), this);
        }
        else if constexpr (std::is_same_v<T, changed_meta_t>) {
            LOG_WARN(log, "{}, changes in '{}' are ignored (not implemented)", identity, r.file->get_full_name());
        }
        else if constexpr (std::is_same_v<T, incomplete_t>) {
            auto ir = initiate_rehash(task, r.file);
            if (ir) {
                stop_processing = true;
            } else {
                auto& path = r.file->get_path();
                model::io_errors_t errors{model::io_error_t{path, ir.assume_error()}};
                send<model::payload::io_error_t>(coordinator, std::move(errors));
            }
        } else {
            static_assert(always_false_v<T>, "non-exhaustive visitor!");
        }
    }, r);

    if (!stop_processing) {
        send<payload::scan_progress_t>(address, std::move(task), gen);
    }  else if(completed) {
        LOG_DEBUG(log, "{}, completed scanning of {}", identity, folder_id);
        generation = 0;
        process_queue();
    }
}

auto scan_actor_t::initiate_rehash(scan_task_ptr_t task, model::file_info_ptr_t file) noexcept -> outcome::result<void> {
    bio::mapped_file_params params;
    params.path = file->get_path().string();
    params.flags = bio::mapped_file::mapmode::readonly;
    auto bio_file = bio_file_t{ new bio::mapped_file()};

    try {
        bio_file->open(params);
    }  catch (std::exception& ex) {
        LOG_WARN(log, "{}, error opening file: {}", identity, ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        return ec;
    };


    send<payload::rehash_needed_t>(coordinator, std::move(task), generation, std::move(file), std::move(bio_file),
                                   int64_t{0}, int64_t{-1}, size_t{0}, std::set<std::int64_t>{}, false, false);
    return outcome::success();
}


void scan_actor_t::on_rehash(message::rehash_needed_t &message) noexcept {
    LOG_TRACE(log, "{}, on_rehash", identity);
    bool can_process_more = rehash_next(message);
    if (can_process_more) {
        process_queue();
    }
}

bool scan_actor_t::rehash_next(message::rehash_needed_t &message) noexcept {
    auto& info = message.payload;
    if (!info.abandoned && !info.invalid) {
        auto condition = [&]() {
            return requested_hashes < requested_hashes_limit && info.last_queued_block < info.file->get_blocks().size();
        };

        auto block_sz = info.file->get_block_size();
        auto file_sz = info.file->get_size();
        auto non_zero = [](char it) { return it != 0;};
        while(condition()) {
            auto& i = info.last_queued_block;
            auto next_size = ((i + 1) * block_sz) > file_sz ? file_sz - (i * block_sz) : block_sz;
            auto ptr = info.mmaped_file->const_data() + (i * block_sz);
            auto begin = (char*) ptr;
            auto end = begin + next_size;
            auto it = std::find_if(begin, end, non_zero);
            if (it == end) { // we have only zeroes
                info.abandoned = true;
            }
            auto block = std::string_view(begin, next_size );
            using request_t = hasher::payload::digest_request_t;
            request<request_t>(hasher_proxy, block, (size_t)i, r::message_ptr_t(&message)).send(init_timeout);
            ++info.queue_size;
            ++i;
            ++requested_hashes;
        }
    }
    return requested_hashes < requested_hashes_limit;
}

void scan_actor_t::on_hash(hasher::message::digest_response_t &res) noexcept {
    --requested_hashes;

    auto& ee = res.payload.ee;
    auto& rp = res.payload.req->payload.request_payload;
    auto msg = static_cast<message::rehash_needed_t*>(rp.custom.get());
    auto& info = msg->payload;
    auto& file = info.file;
    --info.queue_size;

    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        LOG_ERROR(log, "{}, on_hash, file: {}, error: {}", identity, file->get_full_name(), ee->message());
        return do_shutdown(ee);
    }

    bool queued_next = false;
    if (!info.invalid) {
        auto& digest = res.payload.res.digest;
        auto block_index = rp.block_index;
        auto& orig_block = file->get_blocks().at(block_index);
        if (orig_block->get_hash() == digest) {
            auto& ooo = info.out_of_order;
            if (block_index == info.valid_blocks + 1) {
                ++info.valid_blocks;
            } else {
                ooo.insert((int64_t)block_index);
            }
            auto it = ooo.begin();
            while(*it == info.valid_blocks + 1) {
                ++info.valid_blocks;
                it = ooo.erase(it);
            }
            bool ok = rehash_next(*msg);
            queued_next = ok;
            bool complete = (info.queue_size == 0) && (info.abandoned || ((size_t)info.valid_blocks == info.file->get_blocks().size()));
            if (complete) {
                auto diff = model::diff::block_diff_ptr_t{};
                diff = new model::diff::modify::blocks_availability_t(*file, (size_t)info.valid_blocks);
                send<model::payload::block_update_t>(coordinator, std::move(diff), this);
            }
        } else {
            info.invalid = true;
        }
    }

    if (info.invalid && info.queue_size == 0) {
        LOG_DEBUG(log, "{}, removing temporal of '{}' as it corrupted", identity, file->get_full_name());
        auto path = make_temporal(file->get_path());
        sys::error_code ec;
        bfs::remove(path);
        if (ec) {
            model::io_errors_t errors{model::io_error_t{path, ec}};
            send<model::payload::io_error_t>(coordinator, std::move(errors));
        }
    }

    if (!queued_next) {
        process_queue();
    }
}
