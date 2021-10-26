#include "devices.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

void devices_t::apply(cluster_t &cluster) const noexcept {
    auto& device_map = cluster.get_devices();
    auto& local_device = cluster.get_device();
    for(auto& pair:devices) {
        auto device = device_ptr_t();
        if (pair.key == local_device->get_key()) {
            device = local_device;
        } else {
            device = new device_t(pair.key, pair.value);
        }
        device_map.put(device);
    }
    assert(device_map.by_sha256(local_device->device_id().get_sha256()));
}
