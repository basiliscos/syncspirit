#include "log_utils.h"

namespace syncspirit::fltk {

const char *eol =
#ifdef _WIN32
    "\r\n"
#else
    "\n"
#endif
    ;

} // namespace syncspirit::fltk
