#pragma once
#include <array>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace warp {

struct AudioBuffer {
    std::vector<float> data; // interleaved
    int frames = 0;
    int channels = 1;
    int sample_rate = 44100;
};

struct PitchEstimate {
    bool valid = false;
    float frequency = 0.0f;
    float midi = 60.0f;
    int nearest_midi = 60;
    float cents = 0.0f;
    float confidence = 0.0f;
};

enum class LengthAnchor { Percent = 0, Milliseconds = 1 };

struct SampleWarpState {
    bool warp = true;
    float trim_start = 0.0f;
    float trim_end = 1.0f;
    int root_note = 60; // Ableton display convention: MIDI 60 = C3
    float length_percent = 100.0f;
    float length_ms = 0.0f;
    float grain_ms = 45.0f;
    LengthAnchor anchor = LengthAnchor::Percent;
};

struct CacheEntry {
    bool valid = false;
    int midi_note = -1;
    AudioBuffer audio;
};

struct PitchCache {
    std::array<CacheEntry, 128> notes;
    void invalidate();
};

bool read_wav(const std::string &path, AudioBuffer &out, std::string &err);
bool write_wav16(const std::string &path, const AudioBuffer &in, std::string &err);
AudioBuffer trim_audio(const AudioBuffer &src, float start_norm, float end_norm);
float duration_ms(const AudioBuffer &src);
void reconcile_length(SampleWarpState &s, float trimmed_ms, LengthAnchor changed);
PitchEstimate detect_pitch(const AudioBuffer &src, float min_hz = 45.0f, float max_hz = 1200.0f);
AudioBuffer render_constant_duration(const AudioBuffer &src,
                                     float semitones,
                                     int target_frames,
                                     float grain_ms);
float midi_to_hz(float midi);
std::string midi_note_name_ableton(int midi);
int parse_midi_note(const std::string &s, int fallback = 60);

class CachedSample {
public:
    AudioBuffer source;
    SampleWarpState state;
    PitchCache cache;
    int transpose = 0;
    int fine_cents = 0;

    void set_source(AudioBuffer a);
    float trimmed_ms() const;
    int target_frames(int output_sr = 44100) const;
    void invalidate();
    bool is_cached(int midi_note) const;
    void prewarm(int low_midi_note, int high_midi_note, int output_sr = 44100);
    const AudioBuffer *get_or_render(int midi_note, int output_sr = 44100);
};

} // namespace warp
