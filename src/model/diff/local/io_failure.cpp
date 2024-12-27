#include "io_failure.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::local;

io_failure_t::io_failure_t(io_error_t e) noexcept : errors{std::move(e)} {}

io_failure_t::io_failure_t(io_errors_t errors_) noexcept : errors{std::move(errors_)} {}

auto io_failure_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting io_failure_t");
    return visitor(*this, custom);
}
