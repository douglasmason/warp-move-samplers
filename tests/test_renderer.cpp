#include "warp_core.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Exercise real output, including stereo isolation, stretched tails, pitch,
// and rate-dependent cache entries. The production runner links Bungee.
int main() {
    for (int sample_rate : {44100, 48000}) {
        warp::AudioBuffer source;
        source.frames = sample_rate / 2;
        source.sample_rate = sample_rate;
        source.channels = 2;
        source.data.resize(source.frames * 2);
        for (int frame = 0; frame < source.frames; ++frame) {
            source.data[frame*2] = .5f * std::sin(2 * 3.141592653589793 * 440 * frame / sample_rate);
            source.data[frame*2+1] = .3f * std::sin(2 * 3.141592653589793 * 660 * frame / sample_rate);
        }
        for (float duration_ratio : {.5f, 1.f, 2.f})
            for (float semitones : {-12.f, 0.f, 12.f})
                for (float grain_ms : {15.f, 45.f, 120.f}) {
                    const int target_frames = int(source.frames * duration_ratio);
                    auto result = warp::render_constant_duration(source, semitones, target_frames, grain_ms);
                    assert(result.frames == target_frames && result.channels == 2);
                    for (int channel = 0; channel < 2; ++channel) {
                        int crossings = 0;
                        double energy = 0;
                        const int begin = target_frames / 5;
                        const int end = target_frames * 4 / 5;
                        for (int frame = begin; frame < end; ++frame) {
                            const float value = result.data[frame*2+channel];
                            assert(std::isfinite(value));
                            energy += value*value;
                            if (value > 0 && result.data[(frame-1)*2+channel] <= 0) ++crossings;
                        }
                        const double measured = double(crossings) * sample_rate / (end-begin);
                        const double expected = (channel == 0 ? 440 : 660) * std::pow(2., semitones/12.);
                        assert(std::abs(measured-expected) < 12);
                        assert(energy / (end-begin) > .005);
                        double tail_energy = 0;
                        for (int frame = target_frames*9/10; frame < target_frames*19/20; ++frame) {
                            const float value = result.data[frame*2+channel];
                            tail_energy += value*value;
                        }
                        assert(tail_energy / (target_frames/20) > .002);
                    }
                }
        warp::CachedSample cached;
        cached.set_source(source);
        assert(cached.get_or_render(60, 44100)->sample_rate == 44100);
        const auto *second_rate = cached.get_or_render(60, 48000);
        assert(second_rate->sample_rate == 48000 && second_rate->frames == 24000);
    }
    std::cout << "54 stereo renders (108 channel checks) and sample-rate cache checks passed\n";
}
