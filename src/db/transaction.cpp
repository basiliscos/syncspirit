#include "transaction.h"
#include "error_code.h"

namespace syncspirit::db {

transaction_t::transaction_t(transaction_t &&other) noexcept {
    std::swap(type, other.type);
    std::swap(txn, other.txn);
    std::swap(dbi, other.dbi);
}

transaction_t &transaction_t::operator=(transaction_t &&other) noexcept {
    std::swap(type, other.type);
    std::swap(txn, other.txn);
    std::swap(dbi, other.dbi);
    return *this;
}

transaction_t::~transaction_t() {
    if (txn) {
        auto env = mdbx_txn_env(txn);
        mdbx_dbi_close(env, dbi);
        mdbx_txn_commit(txn);
    }
}

outcome::result<transaction_t> transaction_t::make(transaction_type_t type, MDBX_env *env_) noexcept {
    MDBX_txn *txn = nullptr;
    auto r = mdbx_txn_begin(env_, NULL, type == transaction_type_t::RO ? MDBX_TXN_RDONLY : MDBX_TXN_READWRITE, &txn);
    if (r != MDBX_SUCCESS) {
        goto FAILURE;
    }

    MDBX_dbi dbi;
    r = mdbx_dbi_open(txn, nullptr, MDBX_DB_DEFAULTS, &dbi);
    if (r != MDBX_SUCCESS) {
        goto FAILURE;
    }
    return outcome::success(transaction_t(type, txn, dbi));
FAILURE:
    if (txn) {
        mdbx_txn_abort(txn);
    }
    return outcome::failure(make_error_code(r));
}

outcome::result<void> transaction_t::commit() noexcept {
    auto r = mdbx_txn_commit(txn);
    if (r == MDBX_SUCCESS) {
        return outcome::success();
    }
    return outcome::failure(make_error_code(r));
}

} // namespace syncspirit::db