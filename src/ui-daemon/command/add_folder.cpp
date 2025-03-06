// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "add_folder.h"
#include "../governor_actor.h"
#include "../error_code.h"
#include "utils/format.hpp"
#include "utils/base32.h"
#include "model/diff/modify/upsert_folder.h"
#include "pair_iterator.h"
#include <random>

namespace bfs = std::filesystem;

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> add_folder_t::construct(std::string_view in) noexcept {
    std::string_view label, path;
    std::string id;

    auto it = pair_iterator_t(in);
    bool skip_colon = false;
    while (true) {
        auto r = it.next(skip_colon);
        if (r) {
            auto &v = r.value();
            if (v.first == "label") {
                label = v.second;
            } else if (v.first == "path") {
                path = v.second;
            } else if (v.first == "id") {
                id = v.second;
                skip_colon = true;
            }
        } else {
            break;
        }
    }

    if (label.empty()) {
        return make_error_code(error_code_t::missing_folder_label);
    }
    if (path.empty()) {
        return make_error_code(error_code_t::missing_folder_path);
    }
    if (id.empty()) {
        std::random_device rd;
        std::uniform_int_distribution<std::uint64_t> distribution;
        std::mt19937 generator(rd());
        auto intId = distribution(generator);
        auto id_view = utils::bytes_view_t((const unsigned char *)&intId, sizeof(intId));
        auto raw = utils::base32::encode(id_view);
        id = raw.substr(0, raw.size() / 2);
        id += "-";
        id += raw.substr(raw.size() / 2);
    }

    db::Folder f;
    db::set_path(f, path);
    db::set_id(f, id);
    db::set_label(f, label);
    db::set_read_only(f, false);
    db::set_ignore_permissions(f, false);
    db::set_ignore_delete(f, false);
    db::set_disable_temp_indexes(f, false);
    db::set_paused(f, false);
    db::set_scheduled(f, false);
    db::set_folder_type(f, db::FolderType::send_and_receive);
    db::set_pull_order(f, db::PullOrder::random);
    db::set_rescan_interval(f, 3600);
    return command_ptr_t(new add_folder_t(std::move(f)));
}

bool add_folder_t::execute(governor_actor_t &actor) noexcept {
    using namespace model::diff;
    log = actor.log;
    auto &cluster = actor.cluster;

    for (auto it : cluster->get_folders()) {
        auto f = it.item;
        if ((f->get_id() == db::get_id(folder))) {
            log->warn("{}, folder with id = {} is already present is the cluster", actor.get_identity(), f->get_id());
            return false;
        }
        if (f->get_label() == db::get_label(folder)) {
            log->warn("{}, folder with label = {} is already present is the cluster", actor.get_identity(),
                      f->get_label());
            return false;
        }
        if (f->get_path().string() == db::get_path(folder)) {
            log->warn("{}, folder with path = {} is already present is the cluster", actor.get_identity(),
                      f->get_path());
            return false;
        }
    }

    auto opt = modify::upsert_folder_t::create(*cluster, *actor.sequencer, folder, 0);
    if (opt.has_error()) {
        auto message = opt.assume_error().message();
        log->warn("{}, cannot create folder '{}' on '{}': {}", actor.get_identity(), db::get_label(folder), db::get_path(folder),
                  message);
        return false;
    }

    log->debug("{}, going to add folder '{}' on '{}'", actor.get_identity(), db::get_label(folder), db::get_path(folder));

    actor.send_command(std::move(opt.value()), *this);
    return true;
}

} // namespace syncspirit::daemon::command
