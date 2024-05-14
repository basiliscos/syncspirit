#include "log_utils.h"

namespace syncspirit::fltk {

void write(std::ostream &out, const log_record_t &row, std::string_view separator) {
    out << "(" << static_cast<int>(row.level) << ")" << separator << row.date << separator << row.thread_id << separator
        << row.source << separator << row.message;
    ;
}

} // namespace syncspirit::fltk
