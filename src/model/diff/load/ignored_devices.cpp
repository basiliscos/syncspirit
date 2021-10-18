#include "ignored_devices.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

void ignored_devices_t::apply(cluster_t &cluster) const noexcept {
    auto& map = cluster.get_ignored_devices();
    for(auto& pair:devices) {
        auto device = ignored_device_ptr_t(new ignored_device_t(pair.key, pair.value));
        map.put(device);
    }
}
