#include "../common/sampler_engine.h"
extern "C" __attribute__((visibility("default"))) plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    return sampler::initialize(host, false);
}
