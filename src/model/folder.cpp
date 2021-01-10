#include "folder.h"

using namespace syncspirit::model;

folder_t::folder_t(const config::folder_config_t &cfg) noexcept
    : id{cfg.id}, label{cfg.label}, path{cfg.path}, folder_type{cfg.folder_type}, rescan_interval{cfg.rescan_interval},
      pull_order{cfg.pull_order}, watched{cfg.watched}, ignore_permissions{cfg.ignore_permissions} {}
