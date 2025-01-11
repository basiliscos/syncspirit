// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "log-setup.h"

#include "error_code.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <unordered_map>
#include <string_view>
#include <vector>
#include <filesystem>

namespace syncspirit::utils {

namespace bfs = std::filesystem;
namespace sys = boost::system;

static const char *log_pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%L/%t%$] {%n} %v";

using sink_option_t = outcome::result<spdlog::sink_ptr>;

static sink_option_t make_sink(std::string_view name) noexcept {
    if (name == "stdout") {
        return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else if (name == "stderr") {
        return std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    } else if (name.size() > 5 && name.substr(0, 5) == "file:") {
        auto path = std::string(name.substr(5));
        return std::make_shared<spdlog::sinks::basic_file_sink_mt>(path);
    }
    return utils::make_error_code(error_code_t::unknown_sink);
}

void set_default(const std::string &level) noexcept { spdlog::default_logger_raw()->set_level(get_log_level(level)); }

outcome::result<void> init_loggers(const config::log_configs_t &configs, bool overwrite_default) noexcept {
    using sink_map_t = std::unordered_map<std::string, spdlog::sink_ptr>;
    using logger_map_t = std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>;

    // init sinks
    auto prev = spdlog::default_logger();
    using log_t = decltype(prev);
    if (prev->sinks().size() != 1) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
    }
    auto prev_sink = prev->sinks().front();
    auto dist_sink = dynamic_cast<spdlog::sinks::dist_sink_mt *>(prev_sink.get());
    if (!dist_sink) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
    }

    // drop colorized stderr sink
    for (auto &s : dist_sink->sinks()) {
        auto sink = dynamic_cast<spdlog::sinks::stderr_color_sink_mt *>(s.get());
        if (sink) {
            dist_sink->remove_sink(s);
            break;
        }
    }

    spdlog::drop_all();
    spdlog::set_default_logger(prev);

    sink_map_t sink_map;
    for (auto &cfg : configs) {
        for (auto &sink : cfg.sinks) {
            auto sink_option = make_sink(sink);
            if (!sink_option) {
                return sink_option.error();
            }
            sink_map[sink] = sink_option.value();
        }
    }

    logger_map_t logger_map;
    // init default
    std::vector<spdlog::sink_ptr> default_sinks;
    auto root_level = spdlog::level::debug;
    for (auto &cfg : configs) {
        auto &name = cfg.name;
        if (name != "default") {
            continue;
        }
        for (auto &sink_name : cfg.sinks) {
            auto &sink = sink_map.at(sink_name);
            dist_sink->add_sink(sink);
        }
        prev->set_level(cfg.level);
        root_level = cfg.level;
        logger_map[name] = prev;
    }

    if (dist_sink->sinks().size() == 1) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
    }

    // init others
    for (auto &cfg : configs) {
        auto &name = cfg.name;
        if (name == "default") {
            continue;
        }

        std::vector<spdlog::sink_ptr> sinks;
        for (auto &sink_name : cfg.sinks) {
            sinks.push_back(sink_map.at(sink_name));
        }
        if (sinks.empty()) {
            sinks = default_sinks;
        }

        auto log_name = (name == "default") ? "" : name;
        auto logger = std::make_shared<spdlog::logger>(log_name, sinks.begin(), sinks.end());
        auto level = std::max(cfg.level, root_level);
        logger->set_level(level);
        logger_map[name] = logger;
    }

    // register
    for (auto &it : logger_map) {
        auto &name = it.first;
        auto &logger = it.second;
        if (name == "default") {
            if (overwrite_default && prev) {
                logger->set_level(prev->level());
            }
        } else {
            spdlog::register_logger(logger);
        }
    }
    spdlog::set_pattern(log_pattern);
    return outcome::success();
}

static const char *bootstrap_sink = "syncspirit-bootstrap.log";

boostrap_guard_t::boostrap_guard_t(dist_sink_t dist_sink_, file_sink_t *sink_)
    : dist_sink{dist_sink_}, sink{sink_}, discarded{false} {}

boostrap_guard_t::~boostrap_guard_t() {
    auto logger = spdlog::default_logger_raw();
    auto sinks = logger->sinks();
    if (sinks.size() == 1) {
        auto dist_sink = dynamic_cast<spdlog::sinks::dist_sink_mt *>(sinks.at(0).get());
        if (dist_sink) {
            for (auto &s : dist_sink->sinks()) {
                if (s.get() == sink) {
                    auto filename = sink->filename();
                    auto path = bfs::path(filename);
                    dist_sink->flush();
                    sink->flush();
                    dist_sink->remove_sink(s);

                    if (discarded) {
                        auto ec = sys::error_code{};
                        bfs::remove(path, ec); // ignore error, can't do much
                    }
                }
            }
        }
    }
}

void boostrap_guard_t::discard() { discarded = true; }
auto boostrap_guard_t::get_dist_sink() -> dist_sink_t { return dist_sink; }

dist_sink_t create_root_logger() noexcept {
    auto dist_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", dist_sink);
    logger->set_level(spdlog::level::trace);
    spdlog::drop_all();
    spdlog::set_default_logger(logger);
    spdlog::set_pattern(log_pattern);
    spdlog::set_level(spdlog::level::trace);
    return dist_sink;
}

auto bootstrap(spdlog::sink_ptr sink) noexcept -> boostrap_guard_ptr_t {
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(bootstrap_sink, true);
    auto dist_sink = create_root_logger();
    dist_sink->add_sink(console_sink);
    dist_sink->add_sink(file_sink);
    if (sink) {
        dist_sink->add_sink(sink);
    }
    spdlog::trace("bootstrap logger has been initialized");
    return std::make_unique<boostrap_guard_t>(dist_sink, file_sink.get());
}

} // namespace syncspirit::utils
