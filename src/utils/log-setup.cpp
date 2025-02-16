// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "log-setup.h"

#include "error_code.h"
#include "io.h"
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

outcome::result<void> init_loggers(const config::log_configs_t &configs) noexcept {
    using sink_map_t = std::unordered_map<std::string, spdlog::sink_ptr>;
    using logger_map_t = std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>;

    // init sinks
    auto prev = spdlog::default_logger();
    if (prev->sinks().size() != 1) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
    }
    auto prev_sink = prev->sinks().front();
    auto dist_sink = dynamic_cast<spdlog::sinks::dist_sink_mt *>(prev_sink.get());
    if (!dist_sink) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
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
        if (cfg.level == spdlog::level::trace) {
            prev->flush_on(spdlog::level::trace);
        }
        root_level = cfg.level;
        logger_map[name] = prev;
    }

    // if (dist_sink->sinks().size() == 1) {
    //     return utils::make_error_code(error_code_t::misconfigured_default_logger);
    // }

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
        if (name != "default") {
            spdlog::register_logger(logger);
        }
    }
    spdlog::set_pattern(log_pattern);
    return outcome::success();
}

static const char *bootstrap_sink = "syncspirit-bootstrap.log";

bootstrap_guard_t::bootstrap_guard_t(dist_sink_t dist_sink_, spdlog::sinks::sink *sink_)
    : dist_sink{dist_sink_}, sink{sink_} {}

bootstrap_guard_t::~bootstrap_guard_t() {
    auto logger = spdlog::default_logger_raw();
    auto sinks = logger->sinks();
    if (sinks.size() == 1) {
        auto dist_sink = dynamic_cast<spdlog::sinks::dist_sink_mt *>(sinks.at(0).get());
        if (dist_sink) {
            for (auto &s : dist_sink->sinks()) {
                if (s.get() == sink) {
                    dist_sink->flush();
                    sink->flush();
                    dist_sink->remove_sink(s);
                }
            }
        }
    }
}

auto bootstrap_guard_t::get_dist_sink() -> dist_sink_t { return dist_sink; }

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

auto bootstrap(dist_sink_t &dist_sink, const bfs::path &dir) noexcept -> bootstrap_guard_ptr_t {
    using F = fstream_t;
    auto file_path = dir / bootstrap_sink;
    auto file = fstream_t(file_path, F::trunc | F::out | F::binary);
    auto file_sink = spdlog::sink_ptr();
    if (file) {
        file.close();
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
        file_sink.reset(new spdlog::sinks::basic_file_sink_mt(file_path.wstring(), true));
#else
        file_sink.reset(new spdlog::sinks::basic_file_sink_mt(file_path.string(), true));
#endif
        dist_sink->add_sink(file_sink);
        spdlog::trace("file sink has been added initialized");
    } else {
        spdlog::trace("file sink '{}' has NOT been added", file_path.string());
    }
    return std::make_unique<bootstrap_guard_t>(dist_sink, file_sink.get());
}

} // namespace syncspirit::utils
