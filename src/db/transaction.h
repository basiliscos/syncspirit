#pragma once

#include "mdbx.h"
#include "cursor.h"
#include <functional>
#include <boost/outcome.hpp>

namespace syncspirit {
namespace db {

namespace outcome = boost::outcome_v2;

struct transaction_t;

using tx_fn_t = std::function<bool(transaction_t &)>;

enum class transaction_type_t { RO, RW };

struct transaction_t {
    transaction_t() noexcept : txn{nullptr} {};
    transaction_t(transaction_t &&other) noexcept;
    ~transaction_t();

    transaction_t &operator=(transaction_t &&other) noexcept;

    outcome::result<void> commit() noexcept;
    outcome::result<cursor_t> cursor() noexcept;
    outcome::result<std::uint64_t> next_sequence() noexcept;

    static outcome::result<transaction_t> make(transaction_type_t type, MDBX_env *env_) noexcept;

    MDBX_txn *txn = nullptr;
    MDBX_dbi dbi;
    transaction_type_t type;

  private:
    transaction_t(transaction_type_t type_, MDBX_txn *txn_, MDBX_dbi dbi_) noexcept
        : txn{txn_}, dbi{dbi_}, type{type_} {}
};

outcome::result<transaction_t> make_transaction(transaction_type_t type, MDBX_env *env_) noexcept;
outcome::result<transaction_t> make_transaction(transaction_type_t type, transaction_t& prev) noexcept;

} // namespace db
} // namespace syncspirit
