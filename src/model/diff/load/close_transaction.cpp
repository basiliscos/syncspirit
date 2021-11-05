#include "close_transaction.h"
#include <spdlog.h>

using namespace syncspirit::model::diff::load;

close_tranaction_t::close_tranaction_t(db::transaction_t txn_) noexcept: txn{std::move(txn_)} {

}

auto close_tranaction_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return outcome::success();
}


close_tranaction_t::~close_tranaction_t() noexcept {
    auto r = txn.commit();
    if (!r) {
        spdlog::critical("error closing tranaction:: {}", r.error().message());
    }
}
