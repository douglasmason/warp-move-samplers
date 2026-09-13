#include <cassert>
#include <iostream>
#ifdef TEST_DRUMS
#include "../src/drums/plugin.cpp"
#else
#include "../src/melodic/plugin.cpp"
#endif

static AudioBuffer tone() {
    AudioBuffer audio;
    audio.frames = 4410;
    audio.data.resize(audio.frames);
    for (int frame = 0; frame < audio.frames; ++frame)
        audio.data[frame] = .5f * std::sin(2 * 3.141592653589793 * 440 * frame / 44100);
    return audio;
}

int main() {
    host_api_v1_t host{};
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    auto *api = move_plugin_init_v2(&host);
    auto *instance = static_cast<Inst *>(api->create_instance("/tmp/warp-regression", "{}"));
    int16_t output[256]{};
    uint8_t start[] = {0x90, 36, 100};
    uint8_t stop[] = {0x80, 36, 0};
#ifdef TEST_DRUMS
    instance->p[0].s.set_source(tone());
    instance->p[0].s.state.root_note = 69;
    api->set_param(instance, "pad_mode", "gate");
    api->set_param(instance, "pad_decay_ms", "0");
    api->on_midi(instance, start, 3, 2);
    api->render_block(instance, output, 128);
    api->on_midi(instance, stop, 3, 2);
    api->render_block(instance, output, 128);
    assert(std::all_of(std::begin(output), std::end(output), [](int16_t value) { return value == 0; }));
    api->set_param(instance, "pad_tune", "12.5");
    const AudioBuffer *pitched = instance->p[0].s.get_or_render(69);
    int crossings = 0;
    for (int frame = 1; frame < pitched->frames; ++frame)
        if (pitched->data[frame - 1] <= 0 && pitched->data[frame] > 0) ++crossings;
    assert(crossings > 85 && crossings < 95);
    api->on_midi(instance, start, 3, 2);
    uint8_t panic[] = {0xB0, 123, 0};
    api->on_midi(instance, panic, 3, 2);
    api->render_block(instance, output, 128);
    assert(std::all_of(std::begin(output), std::end(output), [](int16_t value) { return value == 0; }));
    // Real note 118 is not a hardware Sample-button event.
    uint8_t high_note[] = {0x90, 118, 100};
    api->on_midi(instance, high_note, 3, 0);
    assert(!instance->recording);
    uint8_t input_memory[512]{};
    host.mapped_memory = input_memory;
    api->set_param(instance, "record", "on");
    const size_t capture_capacity = instance->capture.capacity();
    api->set_param(instance, "current_pad", "2");
    assert(instance->record_pad == 0);
    for (int block = 0; block < 44100*31/128; ++block)
        api->render_block(instance, output, 128);
    assert(instance->capture.size() == 44100*30*2);
    assert(instance->capture.capacity() == capture_capacity);
#else
    instance->sample.set_source(tone());
    instance->sample.state.root_note = 69;
    api->on_midi(instance, start, 3, 2);
    api->set_param(instance, "length_pct", "200");
    api->render_block(instance, output, 128);
    for (const auto &voice : instance->voices) assert(!voice.on);
    api->set_param(instance, "gain", "0.25");
    char value[128]{};
    assert(api->get_param(instance, "gain", value, sizeof(value)) > 0);
    assert(std::abs(std::atof(value) - .25) < .001);
#endif
    api->destroy_instance(instance);
    std::cout << "behavior regressions passed\n";
}
