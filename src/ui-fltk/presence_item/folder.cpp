// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "folder.h"
#include "symbols.h"
#include "table_widget/checkbox.h"
#include "table_widget/choice.h"
#include "table_widget/input.h"
#include "table_widget/label.h"
#include "content/folder_widget.h"
#include "proto/proto-helpers-db.h"
#include "presentation/folder_presence.h"
#include "constants.h"
#include "model/diff/diff_assembler.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/remove_blocks.h"
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <spdlog/fmt/fmt.h>
#include <FL/Fl_Tabs.H>
#include <FL/platform.H>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::presence_item;

static constexpr int padding = 2;

namespace {

struct widget_t : content::folder_widget_t {
    using parent_t = content::folder_widget_t;
    using parent_t::parent_t;

    void on_apply() noexcept override {
        serialization_context_t ctx;
        auto valid = store(&ctx);
        if (!valid) {
            return;
        }

        auto &sup = container.supervisor;
        auto &folder_db = ctx.folder;
        auto log = sup.get_logger();
        auto &cluster = *sup.get_cluster();

        auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder_db, ctx.index);
        if (!opt) {
            log->error("cannot create folder: {}", opt.assume_error().message());
            return;
        }
        auto assember = model::diff::diff_assember_t(constants::diffs_batch);
        assember.push_back(opt.assume_value().get());

        if (shared_with_orig.size()) {
            auto folder = cluster.get_folders().by_id(folder_orig->get_id());
            auto orphaned_blocks = model::orphaned_blocks_t{};
            auto &folder_infos = folder->get_folder_infos();
            for (auto it : shared_with_orig) {
                auto &device = it.item;
                if (!ctx.shared_with.by_sha256(device->device_id().get_sha256())) {
                    auto folder_info = folder_infos.by_device(*device);
                    if (folder_info) {
                        log->info("going to unshare folder '{}' with {}({})", folder->get_label(), device->get_name(),
                                  device->device_id().get_short());
                        auto sub_diff = model::diff::cluster_diff_ptr_t{};
                        assember.push_back(new modify::unshare_folder_t(cluster, *folder_info, &orphaned_blocks));
                    }
                }
            }
            if (auto orphaned_set = orphaned_blocks.deduce(); orphaned_set.size()) {
                log->info("going to remove {} orphaned blocks", orphaned_set.size());
                auto sub_diff = model::diff::cluster_diff_ptr_t{};
                assember.push_back(new modify::remove_blocks_t(std::move(orphaned_set)));
            }
        }

        auto devices = std::vector<utils::bytes_t>{};
        for (auto it : non_shared_with_orig) {
            auto &device = it.item;
            auto sha256 = device->device_id().get_sha256();
            if (ctx.shared_with.by_sha256(sha256)) {
                devices.emplace_back(utils::bytes_t(sha256.begin(), sha256.end()));
            }
        }

        auto folder_id = db::get_id(folder_db);
        auto cb =
            devices.empty() ? sup.call_select_folder(folder_id) : sup.call_share_folders(folder_id, std::move(devices));
        sup.send_model<model::payload::model_update_t>(assember.consume(), cb.get());
    }
};

} // namespace

folder_t::folder_t(presentation::folder_presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(presence, supervisor, tree) {
    update_label();
    populate_dummy_child();
}

folder_t::~folder_t() {
    if (auto p = parent(); p) {
        auto index = p->find_child(this);
        if (index >= 0) {
            if (is_selected()) {
                select_other();
            }
            p->deparent(index);
            if (auto ti = dynamic_cast<tree_item_t *>(p); ti) {
                ti->update_label();
            }
        }
    }
}

bool folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using B = content::folder_widget_t::behavior_t;
        auto prev = content->get_widget();

        auto fp = static_cast<presentation::folder_presence_t *>(presence);
        auto &folder_info = fp->get_folder_info();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto is_local = folder_info.get_device() == folder_info.get_folder()->get_cluster()->get_device();
        auto b = is_local ? B::local : B::remote;
        auto widget = new widget_t(*this, b, x, y, w, h);
        widget->make_tabs(folder_info.get_folder(), &folder_info);
        return widget;
    });
    return true;
}
