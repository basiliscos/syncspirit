#include "add_folder.h"
#include "../governor_actor.h"
#include "../error_code.h"
#include "../../utils/base32.h"
#include "pair_iterator.h"
#include <random>

namespace bfs = boost::filesystem;

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> add_folder_t::construct(std::string_view in) noexcept {
    std::string_view label, path;
    std::string id;

    auto it = pair_iterator_t(in);
    while (true) {
        auto r = it.next();
        if (r) {
            auto &v = r.value();
            if (v.first == "label") {
                label = v.second;
            } else if (v.first == "path") {
                path = v.second;
            } else if (v.first == "id") {
                id = v.second;
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
        auto id_view = std::string((char *)&intId, sizeof(intId));
        auto raw = utils::base32::encode(id_view);
        id = raw.substr(0, raw.size() / 2);
        id += "-";
        id += raw.substr(raw.size() / 2);
    }

    db::Folder f;
    f.set_path(std::string(path));
    f.set_id(id);
    f.set_label(std::string(label));
    f.set_read_only(false);
    f.set_ignore_permissions(true);
    f.set_ignore_delete(false);
    f.set_disable_temp_indexes(true);
    f.set_paused(false);
    f.set_watched(true);
    f.set_folder_type(db::FolderType::send_and_receive);
    f.set_pull_order(db::PullOrder::random);
    f.set_rescan_interval(3600);
    return command_ptr_t(new add_folder_t(std::move(f)));
}

bool add_folder_t::execute(governor_actor_t &actor) noexcept {
    log = actor.log;
    bool found = false;
    for (auto it : actor.cluster_copy->get_folders()) {
        auto f = it.second;
        if ((f->id() == folder.id())) {
            log->warn("{}, folder with id = {} is alredy present is the cluster", actor.get_identity(), f->id());
            return false;
        }
        if (f->label() == folder.label()) {
            log->warn("{}, folder with label = {} is alredy present is the cluster", actor.get_identity(), f->label());
            return false;
        }
        if (f->get_path().string() == folder.path()) {
            log->warn("{}, folder with path = {} is alredy present is the cluster", actor.get_identity(),
                      f->get_path());
            return false;
        }
    }
    actor.cmd_add_folder(folder);
    return true;
}

} // namespace syncspirit::daemon::command
