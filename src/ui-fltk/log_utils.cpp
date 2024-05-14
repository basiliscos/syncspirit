#include "log_utils.h"

namespace syncspirit::fltk {

const char *eol =
#ifdef _WIN32
    "\r\n"
#else
    "\n"
#endif
    ;

void write(std::ostream &out, const log_record_t &row) {
    out << "(" << static_cast<int>(row.level) << ")"
        << "\t" << row.date << "\t" << row.thread_id << "\t" << row.source << "\t" << row.message;
    ;
}

} // namespace syncspirit::fltk
