#include "activity.h"
#include "tui_actor.h"

using namespace syncspirit::console;

void activity_t::forget() noexcept {
    actor.activities.pop_front();
    actor.activities.front()->display();
}
