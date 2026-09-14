#pragma once
#include "schwung_api.h"
#include "warp_core.h"
#include "ui_metadata.h"
#include "../../third_party/json.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <algorithm>

// One low-priority worker owns all allocation, files, analysis and preparation.
// The host callback only copies bounded commands and reads immutable snapshots.
namespace sampler {
using Json = nlohmann::json;
constexpr int kInstances = 4, kPads = 16, kVoices = 64, kCommands = 64;
constexpr size_t kValueSize = 32768;
#ifndef SAMPLER_AUDIO_BUDGET
#define SAMPLER_AUDIO_BUDGET (64 * 1024 * 1024)
#endif
constexpr size_t kAudioBudget = SAMPLER_AUDIO_BUDGET;
constexpr size_t kRenderLimit = 12 * 1024 * 1024;
constexpr size_t kCaptureSamples = 48000 * 30 * 2;
constexpr int kPathSize = 512;
static const host_api_v1_t *host = nullptr;
static bool drum_engine = false;
static size_t audio_bytes = 0; // worker-owned, shared across instances
static std::atomic<size_t> audio_bytes_readout{0};
static std::atomic<bool> stopping{false};
static pthread_t worker_thread{};
static bool worker_started = false;

struct Asset {
    warp::AudioBuffer audio;
    explicit Asset(warp::AudioBuffer &&value) : audio(std::move(value)) {
        audio_bytes += audio.data.capacity() * sizeof(float);
        audio_bytes_readout.store(audio_bytes);
    }
    ~Asset() {
        audio_bytes -= audio.data.capacity() * sizeof(float);
        audio_bytes_readout.store(audio_bytes);
    }
};
using AssetPtr = std::shared_ptr<const Asset>;
struct Pad {
    warp::SampleWarpState warp;
    std::string path;
    int transpose=0,fine=0;
    float gain=1, pan=0, tune=0, attack=0, release=250, chance=100;
    bool gate=false;
    int choke=0;
    uint64_t generation=1;
    AssetPtr source;
    std::array<AssetPtr,128> notes{};
    std::array<uint64_t,128> used{};
};
struct Settings {
    int current=0, polyphony=8, default_root=60, slice_count=8;
    bool auto_root=true, slice_analyze=true, pitched=false;
    std::array<Pad,kPads> pads;
};
struct Snapshot {
    Settings settings;
    std::string state, status="Ready", error;
};
struct Command { char key[48]{}; char value[kValueSize]{}; };
struct Miss { uint64_t generation=0; int pad=0,note=0; };
struct Voice {
    const warp::AudioBuffer *audio=nullptr;
    double position=0,increment=1,end=0;
    float gain_left=1,gain_right=1,envelope=1,attack_step=1,release_step=1,decay_step=0;
    int note=0,channel=0,choke=0;
    bool gate=false,releasing=false,attacking=false;
};
struct Instance {
    // life: free / active / closing. Backend exists only on the worker.
    std::atomic<int> life{0};
    char directory[kPathSize]{}, defaults[kValueSize]{};
    std::atomic<Snapshot*> published{nullptr};
    std::array<std::atomic<Snapshot*>,kVoices+1> hazards{};
    std::array<Voice,kVoices> voices{};
    std::array<Command,kCommands> commands{};
    std::atomic<unsigned> command_write{0},command_read{0};
    std::array<Miss,128> misses{};
    std::atomic<unsigned> miss_write{0},miss_read{0};
    std::atomic<bool> overflow{false}, warming{false}, finishing{false};
    std::atomic<int> capture_state{0}; // 0 idle, 1 recording, 2 automatic finalize
    std::atomic<bool> capture_busy{false};
    std::atomic<size_t> captured{0};
    std::array<int16_t,kCaptureSamples> capture{};
    uint32_t random=0x726f6f74;
    bool sample_button=false;
};
static std::array<Instance,kInstances> instances;

static Snapshot *acquire(Instance &instance) {
    Snapshot *snapshot;
    do {
        snapshot=instance.published.load();
        instance.hazards[kVoices].store(snapshot);
    } while(snapshot!=instance.published.load());
    return snapshot;
}
static void release(Instance &instance) { instance.hazards[kVoices].store(nullptr); }
static void stop_voice(Instance &instance,int index) {
    instance.voices[index].audio=nullptr;
    instance.hazards[index].store(nullptr);
}
static bool enqueue(Instance &instance,const char *key,const char *value) {
    const size_t key_size=strnlen(key,48),value_size=strnlen(value,kValueSize);
    const unsigned write=instance.command_write.load(std::memory_order_relaxed);
    if(key_size>=48||value_size>=kValueSize||write-instance.command_read.load(std::memory_order_acquire)>=kCommands) {
        instance.overflow.store(true);return false;
    }
    auto &command=instance.commands[write%kCommands];
    std::memcpy(command.key,key,key_size+1);std::memcpy(command.value,value,value_size+1);
    instance.command_write.store(write+1,std::memory_order_release);return true;
}
static void request_note(Instance &instance,int pad,int note,uint64_t generation) {
    const unsigned write=instance.miss_write.load(std::memory_order_relaxed);
    if(write-instance.miss_read.load(std::memory_order_acquire)>=instance.misses.size())return;
    instance.misses[write%instance.misses.size()]={generation,pad,note};
    instance.miss_write.store(write+1,std::memory_order_release);
}
static int sample_rate(){return host&&host->sample_rate>0?host->sample_rate:44100;}
static bool enabled(const std::string &value){return value=="on"||value=="true"||value=="1";}
static float number(const std::string &value,float low,float high) {
    char *end=nullptr;float result=std::strtof(value.c_str(),&end);
    if(end==value.c_str()||*end||!std::isfinite(result))throw std::runtime_error("Invalid numeric value");
    return std::clamp(result,low,high);
}
static std::string prefix(int pad){char name[8];std::snprintf(name,sizeof(name),"p%02d_",pad+1);return name;}

struct Backend;
static std::array<Backend*,kInstances> registry{};
static uint64_t cache_clock=0;

struct Backend {
    Instance &instance;
    Settings settings;
    std::unique_ptr<Snapshot> current;
    std::vector<std::unique_ptr<Snapshot>> retired;
    std::string status="Ready",error;
    int record_pad=0;
    uint64_t clock=0;
    bool dirty=true,restoring=false;
    std::array<int,kPads> warm_next{},warm_end{};
    explicit Backend(Instance &value):instance(value) {
        warm_next.fill(-1);settings.polyphony=drum_engine?16:8;
        if(!drum_engine){settings.pads[0].attack=5;settings.pads[0].release=150;settings.pads[0].gate=true;}
    }
    void schedule(int index) {
        if(restoring)return;
        const auto &pad=settings.pads[index];
        const int range=drum_engine&&(!settings.pitched||index!=settings.current)?0:12;
        warm_next[index]=pad.source&&pad.warp.warp?std::max(0,pad.warp.root_note-range):-1;
        warm_end[index]=std::min(127,pad.warp.root_note+range);instance.warming.store(true);
    }
    void collect() {
        retired.erase(std::remove_if(retired.begin(),retired.end(),[&](const auto &snapshot){
            for(auto &hazard:instance.hazards)if(hazard.load()==snapshot.get())return false;
            return true;
        }),retired.end());
    }
    static void clear_cache(Pad &pad) {
        for(auto &note:pad.notes)note.reset();
        pad.used.fill(0);++pad.generation;
    }
    static float trimmed_ms(const Pad &pad) {
        if(!pad.source)return 0;
        const int frames=pad.source->audio.frames;
        const int begin=int(std::floor(pad.warp.trim_start*(frames-1)));
        const int end=int(std::ceil(pad.warp.trim_end*(frames-1)))+1;
        return 1000.f*std::max(1,end-begin)/pad.source->audio.sample_rate;
    }
    static void reconcile(Pad &pad) {
        warp::reconcile_length(pad.warp,trimmed_ms(pad),pad.warp.anchor);
    }
    Json state_json() const {
        Json state=Json::object();
        state["state_version"]=2;
        state["auto_root"]=settings.auto_root?"on":"off";
        state["default_root"]=settings.default_root;
        state["polyphony"]=settings.polyphony;
        if(drum_engine){state["current_pad"]=settings.current+1;state["pitched_mode"]=settings.pitched?"on":"off";
            state["slice_analyze"]=settings.slice_analyze?"on":"off";state["slice_count"]=settings.slice_count;}
        for(int index=0;index<(drum_engine?kPads:1);++index) {
            const auto &pad=settings.pads[index];const std::string key=drum_engine?prefix(index):"";
            state[key+"sample_path"]=pad.path;state[key+"warp"]=pad.warp.warp?"on":"off";
            state[key+"root_note"]=pad.warp.root_note;state[key+"trim_start"]=pad.warp.trim_start;state[key+"trim_end"]=pad.warp.trim_end;
            state[key+"length_pct"]=pad.warp.length_percent;state[key+"length_ms"]=pad.warp.length_ms;
            state[key+"length_anchor"]=pad.warp.anchor==warp::LengthAnchor::Percent?"percent":"ms";
            state[key+"grain_ms"]=pad.warp.grain_ms;state[key+"attack_ms"]=pad.attack;
            if(drum_engine){state[key+"vol"]=pad.gain;state[key+"pan"]=pad.pan;state[key+"tune"]=pad.tune;
                state[key+"decay_ms"]=pad.release;state[key+"mode"]=pad.gate?"gate":"oneshot";
                state[key+"choke_group"]=pad.choke;state[key+"chance_pct"]=pad.chance;
            }else{state["gain"]=pad.gain;state["release_ms"]=pad.release;
                state["transpose"]=pad.transpose;state["fine_tune"]=pad.fine;}
        }
        return state;
    }
    void publish() {
        auto next=std::make_unique<Snapshot>();next->settings=settings;
        next->status=status;next->error=error;next->state=state_json().dump();
        Snapshot *pointer=next.get();if(current)retired.push_back(std::move(current));
        current=std::move(next);instance.published.store(pointer);dirty=false;collect();
    }
    bool room(size_t bytes) {
        collect();
        if(restoring&&audio_bytes+bytes>kAudioBudget)return false;
        while(audio_bytes+bytes>kAudioBudget) {
            Pad *victim=nullptr;Backend *owner=nullptr;int victim_note=0;uint64_t oldest=UINT64_MAX;
            for(auto *candidate:registry)if(candidate&&!candidate->restoring)
                for(auto &pad:candidate->settings.pads)for(int note=0;note<128;++note)
                    if(pad.notes[note]&&pad.used[note]<oldest){oldest=pad.used[note];victim=&pad;victim_note=note;owner=candidate;}
            if(!victim)return false;
            victim->notes[victim_note].reset();owner->publish();
        }
        return true;
    }
    AssetPtr adopt(warp::AudioBuffer audio) {
        if(!room(audio.data.capacity()*sizeof(float)))throw std::runtime_error("Audio memory full; release held notes or unload a sample");
        return std::make_shared<Asset>(std::move(audio));
    }
    AssetPtr read_source(const std::string &path) {
        if(path.size()>=kPathSize)throw std::runtime_error("Sample path is too long");
        std::error_code failure;const auto bytes=std::filesystem::file_size(path,failure);
        if(failure||bytes>32*1024*1024)throw std::runtime_error("Sample missing or larger than 32 MB");
        warp::AudioBuffer audio;std::string message;
        if(!warp::read_wav(path,audio,message))throw std::runtime_error(message);
        if(warp::duration_ms(audio)>30000)throw std::runtime_error("Samples are limited to 30 seconds");
        return adopt(std::move(audio));
    }
    void analyze(Pad &pad) {
        if(!pad.source)return;
        auto estimate=warp::detect_pitch(warp::trim_audio(pad.source->audio,pad.warp.trim_start,pad.warp.trim_end));
        if(estimate.valid)pad.warp.root_note=estimate.nearest_midi;
    }
    void load(int index,const std::string &path,bool analyze_root=true) {
        AssetPtr source=path.empty()?AssetPtr{}:read_source(path);
        auto &pad=settings.pads[index];clear_cache(pad);pad.source=std::move(source);pad.path=path;
        pad.warp.trim_start=0;pad.warp.trim_end=1;pad.warp.anchor=warp::LengthAnchor::Percent;pad.warp.length_percent=100;
        pad.warp.root_note=settings.default_root;if(analyze_root&&settings.auto_root)analyze(pad);reconcile(pad);schedule(index);dirty=true;
    }
    void prepare(int index,int note,uint64_t generation) {
        auto &pad=settings.pads[index];
        if(pad.generation!=generation||!pad.source||!pad.warp.warp)return;
        if(pad.notes[note]){pad.used[note]=++cache_clock;return;}
        const double frame_count=std::round(pad.warp.length_ms*.001*sample_rate());
        const double bytes=frame_count*pad.source->audio.channels*sizeof(float);
        if(frame_count<2||bytes>kRenderLimit){error="Warp render exceeds 12 MB; shorten Length";dirty=true;return;}
        if(!room(size_t(bytes))){error="Warp cache full; release held notes";dirty=true;return;}
        // Bounded temporary work exists only on this worker. CachedSample is
        // used for sample-rate normalization and the production renderer.
        warp::CachedSample temporary;temporary.source=pad.source->audio;temporary.state=pad.warp;
        temporary.transpose=int(pad.tune);temporary.fine_cents=int(std::lround((pad.tune-temporary.transpose)*100));
        temporary.get_or_render(note,sample_rate());
        pad.notes[note]=adopt(std::move(temporary.cache.notes[note].audio));pad.used[note]=++cache_clock;dirty=true;
    }
    std::string asset_path(const char *directory,int pad) {
        const auto folder=std::filesystem::path(instance.directory)/directory;
        std::filesystem::create_directories(folder);
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        return (folder/(std::to_string(stamp)+"_"+std::to_string(pad)+"_"+std::to_string(++clock)+".wav")).string();
    }
    void finish_recording() {
        instance.capture_state.store(0);
        while(instance.capture_busy.load())std::this_thread::yield();
        const size_t count=instance.captured.load();
        if(count<64){status="Recording too short";dirty=true;return;}
        warp::AudioBuffer audio;audio.frames=int(count/2);audio.channels=2;audio.sample_rate=sample_rate();audio.data.resize(count);
        for(size_t index=0;index<count;++index)audio.data[index]=instance.capture[index]/32768.f;
        const auto path=asset_path("recordings",record_pad);std::string message;
        if(!warp::write_wav16(path,audio,message))throw std::runtime_error("Could not save recording: "+message);
        auto &pad=settings.pads[record_pad];clear_cache(pad);pad.source=adopt(std::move(audio));pad.path=path;
        pad.warp.trim_start=0;pad.warp.trim_end=1;pad.warp.anchor=warp::LengthAnchor::Percent;pad.warp.length_percent=100;
        pad.warp.root_note=settings.default_root;if(settings.auto_root)analyze(pad);reconcile(pad);schedule(record_pad);status="Recorded";dirty=true;
    }
    void record() {
        if(instance.capture_state.load())finish_recording();
        else{while(instance.capture_busy.load())std::this_thread::yield();record_pad=settings.current;
            std::fill(instance.capture.begin(),instance.capture.end(),int16_t(0));
            instance.captured.store(0);instance.capture_state.store(1);status="Recording";dirty=true;}
    }
    void slice() {
        const Pad parent=settings.pads[settings.current];if(!parent.source)return;
        const auto audio=warp::trim_audio(parent.source->audio,parent.warp.trim_start,parent.warp.trim_end);
        if(audio.frames<settings.slice_count*2)throw std::runtime_error("Sample too short to slice");
        std::vector<Pad> results;
        for(int index=0;index<settings.slice_count;++index) {
            Pad pad=parent;clear_cache(pad);
            const int begin=int(int64_t(audio.frames)*index/settings.slice_count),end=int(int64_t(audio.frames)*(index+1)/settings.slice_count);
            warp::AudioBuffer part;part.frames=end-begin;part.channels=audio.channels;part.sample_rate=audio.sample_rate;
            part.data.assign(audio.data.begin()+size_t(begin)*audio.channels,audio.data.begin()+size_t(end)*audio.channels);
            pad.path=asset_path("slices",index);std::string message;
            if(!warp::write_wav16(pad.path,part,message))throw std::runtime_error("Could not save slice: "+message);
            pad.source=adopt(std::move(part));pad.warp.trim_start=0;pad.warp.trim_end=1;
            pad.warp.anchor=warp::LengthAnchor::Percent;pad.warp.length_percent=100;
            if(settings.slice_analyze)analyze(pad);reconcile(pad);results.push_back(std::move(pad));
        }
        for(size_t index=0;index<results.size();++index){const int target=(settings.current+int(index))%kPads;
            results[index].generation=settings.pads[target].generation+1;settings.pads[target]=std::move(results[index]);schedule(target);}
        status="Sliced";dirty=true;
    }
    void apply(const std::string &key,const std::string &value) {
        dirty=true;
        if(key=="state_version")return;
        if(key=="auto_root"){settings.auto_root=enabled(value);return;}
        if(key=="default_root"){settings.default_root=warp::parse_midi_note(value,settings.default_root);return;}
        if(key=="polyphony"){settings.polyphony=int(number(value,1,drum_engine?64:16));return;}
        if(key=="current_pad"){settings.current=int(number(value,1,16))-1;if(settings.pitched)schedule(settings.current);return;}
        if(key=="pitched_mode"){settings.pitched=enabled(value);if(settings.pitched)schedule(settings.current);return;}
        if(key=="slice_count"){settings.slice_count=int(number(value,2,16));return;}
        if(key=="slice_analyze"){settings.slice_analyze=enabled(value);return;}
        if(key=="record"){record();return;}
        if(key=="slice"){if(drum_engine)slice();return;}
        int index=drum_engine?settings.current:0;std::string field=key;
        if(key.rfind("pad_",0)==0)field=key.substr(4);
        else if(key.size()>4&&key[0]=='p'&&key[1]>='0'&&key[1]<='9'&&key[2]>='0'&&key[2]<='9'&&key[3]=='_'){
            index=(key[1]-'0')*10+(key[2]-'0')-1;field=key.substr(4);
            if(index<0||index>=16)throw std::runtime_error("Invalid pad");
        }
        auto &pad=settings.pads[index];
        if(field=="sample_path"){load(index,value);return;}
        if(field=="gain"||field=="vol"){pad.gain=number(value,0,2);return;}
        if(field=="pan"){pad.pan=number(value,-1,1);return;}
        if(field=="attack_ms"){pad.attack=number(value,0,drum_engine?5000:10000);return;}
        if(field=="release_ms"||field=="decay_ms"){pad.release=number(value,0,drum_engine?5000:10000);return;}
        if(field=="chance_pct"){pad.chance=number(value,0,100);return;}
        if(field=="mode"){pad.gate=value=="gate";return;}
        if(field=="choke_group"){pad.choke=int(number(value,0,16));return;}
        if(field=="warp")pad.warp.warp=enabled(value);
        else if(field=="root_note")pad.warp.root_note=warp::parse_midi_note(value,pad.warp.root_note);
        else if(field=="analyze_root")analyze(pad);
        else if(field=="trim_start"){pad.warp.trim_start=number(value,0,pad.warp.trim_end);reconcile(pad);}
        else if(field=="trim_end"){pad.warp.trim_end=number(value,pad.warp.trim_start,1);reconcile(pad);}
        else if(field=="length_pct"){pad.warp.length_percent=number(value,1,800);pad.warp.anchor=warp::LengthAnchor::Percent;reconcile(pad);}
        else if(field=="length_ms"){pad.warp.length_ms=number(value,1,600000);pad.warp.anchor=warp::LengthAnchor::Milliseconds;reconcile(pad);}
        else if(field=="length_anchor"){pad.warp.anchor=value=="ms"?warp::LengthAnchor::Milliseconds:warp::LengthAnchor::Percent;reconcile(pad);}
        else if(field=="grain_ms")pad.warp.grain_ms=number(value,5,250);
        else if(field=="tune")pad.tune=number(value,-24,24);
        else if(field=="transpose"){pad.transpose=int(number(value,-48,48));pad.tune=pad.transpose+pad.fine/100.f;}
        else if(field=="fine_tune"){pad.fine=int(number(value,-100,100));pad.tune=pad.transpose+pad.fine/100.f;}
        else if(field!="rebuild_cache")throw std::runtime_error("Unknown parameter: "+key);
        clear_cache(pad);schedule(index);
    }
    static std::string text(const Json &value) {return value.is_string()?value.get<std::string>():value.dump();}
    void restore(const std::string &encoded) {
        const auto state=Json::parse(encoded);
        if(!state.is_object())throw std::runtime_error("Expected a sampler state object");
        Settings previous=settings;restoring=true;
        try {
            // Sources first; metadata afterwards. Never run actions from state.
            for(auto iterator=state.begin();iterator!=state.end();++iterator)
                if(iterator.key()=="auto_root"||iterator.key()=="default_root")apply(iterator.key(),text(iterator.value()));
            for(int index=0;index<(drum_engine?16:1);++index){const auto key=(drum_engine?prefix(index):"")+"sample_path";
                if(state.contains(key)&&text(state[key])!=settings.pads[index].path)load(index,text(state[key]),false);}
            for(auto iterator=state.begin();iterator!=state.end();++iterator) {
                const auto key=iterator.key();
                if(key.find("sample_path")!=std::string::npos||key.find("length_")!=std::string::npos||key=="record"||key=="slice"||key=="analyze_root"||key=="rebuild_cache")continue;
                apply(key,text(iterator.value()));
            }
            for(int index=0;index<(drum_engine?16:1);++index) {
                const std::string key=drum_engine?prefix(index):"";
                const std::string anchor=state.value(key+"length_anchor",std::string("percent"));
                if(state.contains(key+"length_pct"))apply(key+"length_pct",text(state[key+"length_pct"]));
                if(anchor=="ms"&&state.contains(key+"length_ms"))apply(key+"length_ms",text(state[key+"length_ms"]));
            }
        }catch(...){settings=std::move(previous);restoring=false;throw;}
        restoring=false;for(int index=0;index<(drum_engine?16:1);++index)schedule(index);dirty=true;
    }
};
static std::array<std::unique_ptr<Backend>,kInstances> backends;

static void *worker(void*) {
#ifdef __linux__
    sched_param priority{};sched_setscheduler(0,SCHED_OTHER,&priority);
    cpu_set_t target;CPU_ZERO(&target);CPU_SET(0,&target);CPU_SET(1,&target);CPU_SET(2,&target);
    // A thread created by SPI inherits its core-3-only affinity. Explicitly
    // widen that mask on Move; merely clearing core 3 would leave an empty set.
    if(sched_setaffinity(0,sizeof(target),&target)!=0){
        cpu_set_t available;CPU_ZERO(&available);
        if(sched_getaffinity(0,sizeof(available),&available)==0){CPU_CLR(3,&available);if(CPU_COUNT(&available)>0)sched_setaffinity(0,sizeof(available),&available);}
    }
#endif
    while(!stopping.load()) {
        for(int index=0;index<kInstances;++index) {
            auto &instance=instances[index];const int life=instance.life.load();
            if(life==0)continue;
            if(life==2){instance.capture_state.store(0);instance.published.store(nullptr);registry[index]=nullptr;backends[index].reset();instance.life.store(0);continue;}
            try {
                if(!backends[index]) {
                    backends[index]=std::make_unique<Backend>(instance);registry[index]=backends[index].get();
                    if(instance.defaults[0])backends[index]->restore(instance.defaults);
                    backends[index]->publish();
                }
                auto &backend=*backends[index];backend.collect();
                unsigned read=instance.command_read.load(std::memory_order_relaxed);
                if(read!=instance.command_write.load(std::memory_order_acquire)) {
                    const auto &command=instance.commands[read%kCommands];backend.error.clear();
                    try{if(std::strcmp(command.key,"state")==0)backend.restore(command.value);else backend.apply(command.key,command.value);}
                    catch(const std::exception &failure){backend.error=failure.what();}
                    if(!instance.capture_state.load())backend.status=backend.error.empty()?"Ready":"Error";
                    backend.publish();instance.command_read.store(read+1,std::memory_order_release);
                    continue;
                }
                if(instance.capture_state.load()==2){instance.finishing.store(true);backend.finish_recording();backend.publish();instance.finishing.store(false);}
                const unsigned miss_read=instance.miss_read.load(std::memory_order_relaxed);
                if(miss_read!=instance.miss_write.load(std::memory_order_acquire)) {
                    const auto miss=instance.misses[miss_read%instance.misses.size()];
                    try{backend.prepare(miss.pad,miss.note,miss.generation);}catch(const std::exception &failure){backend.error=failure.what();backend.dirty=true;}
                    if(backend.dirty)backend.publish();instance.miss_read.store(miss_read+1,std::memory_order_release);
                }else{
                    bool warming=false;
                    for(int pad=0;pad<(drum_engine?16:1);++pad)if(backend.warm_next[pad]>=0){
                        warming=true;const int note=backend.warm_next[pad]++;
                        if(backend.warm_next[pad]>backend.warm_end[pad])backend.warm_next[pad]=-1;
                        backend.prepare(pad,note,backend.settings.pads[pad].generation);
                        if(backend.dirty)backend.publish();break;
                    }
                    instance.warming.store(warming);
                }
            }catch(const std::exception &failure) {
                if(backends[index]){backends[index]->error=failure.what();backends[index]->status="Error";backends[index]->publish();}
                instance.finishing.store(false);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for(int index=0;index<kInstances;++index){instances[index].published.store(nullptr);registry[index]=nullptr;backends[index].reset();}
    return nullptr;
}
static void shutdown();
static void shutdown(){if(worker_started){stopping.store(true);pthread_join(worker_thread,nullptr);worker_started=false;}}

struct Lifetime { ~Lifetime(){shutdown();} };
static Lifetime lifetime;

static void *create(const char *directory,const char *defaults) {
    if(!worker_started||!directory||strnlen(directory,kPathSize)>=kPathSize|| (defaults&&strnlen(defaults,kValueSize)>=kValueSize))return nullptr;
    for(auto &instance:instances)if(instance.life.load()==0) {
        // Host create/destroy and performance callbacks are serialized.
        std::snprintf(instance.directory,sizeof(instance.directory),"%s",directory);
        std::snprintf(instance.defaults,sizeof(instance.defaults),"%s",defaults?defaults:"{}");
        instance.command_write.store(0);instance.command_read.store(0);instance.miss_write.store(0);instance.miss_read.store(0);
        instance.capture_state.store(0);instance.captured.store(0);instance.overflow.store(false);instance.warming.store(false);instance.finishing.store(false);instance.sample_button=false;
        for(int index=0;index<kVoices;++index)stop_voice(instance,index);
        instance.hazards[kVoices].store(nullptr);instance.life.store(1);return &instance;
    }
    return nullptr;
}
static void destroy(void *pointer) {
    if(!pointer)return;auto &instance=*static_cast<Instance*>(pointer);
    for(int index=0;index<kVoices;++index)stop_voice(instance,index);
    instance.hazards[kVoices].store(nullptr);instance.capture_state.store(0);instance.life.store(2);
}
static void set_param(void *pointer,const char *key,const char *value) {
    if(pointer&&key&&value)enqueue(*static_cast<Instance*>(pointer),key,value);
}
static int copy_text(char *buffer,int capacity,const char *text) {
    if(!buffer||capacity<=0)return -1;return std::snprintf(buffer,capacity,"%s",text);
}
static int get_param(void *pointer,const char *key,char *buffer,int capacity) {
    if(!pointer||!key||!buffer||capacity<2)return -1;auto &instance=*static_cast<Instance*>(pointer);
    const bool stock=std::strstr(instance.directory,drum_engine?"warpdrumkit":"warpmelodic")!=nullptr;
    if(!std::strcmp(key,"version"))return copy_text(buffer,capacity,"0.2.0");
    if(!std::strcmp(key,"name"))return copy_text(buffer,capacity,drum_engine?(stock?"Drum Kit+":"WarpMrDrums"):(stock?"Melodic Sampler+":"WarpMrSample"));
    if(!std::strcmp(key,"ui_hierarchy"))return copy_text(buffer,capacity,drum_engine?(stock?warpdrumkit_hierarchy:warpmrdrums_hierarchy):(stock?warpmelodic_hierarchy:warpmrsample_hierarchy));
    if(!std::strcmp(key,"chain_params"))return copy_text(buffer,capacity,drum_engine?(stock?warpdrumkit_params:warpmrdrums_params):(stock?warpmelodic_params:warpmrsample_params));
    if(!std::strcmp(key,"recording"))return copy_text(buffer,capacity,instance.capture_state.load()?"on":"off");
    if(!std::strcmp(key,"audio_bytes"))return std::snprintf(buffer,capacity,"%zu",audio_bytes_readout.load());
    Snapshot *snapshot=acquire(instance);
    int result=-1;
    if(!std::strcmp(key,"status")) {
        const bool pending=instance.finishing.load()||instance.capture_state.load()==2||instance.warming.load()||instance.command_read.load()!=instance.command_write.load()||instance.miss_read.load()!=instance.miss_write.load();
        result=copy_text(buffer,capacity,!snapshot?"Initializing":(pending?"Preparing":snapshot->status.c_str()));
    }else if(snapshot) {
        const auto &settings=snapshot->settings;
        if(!std::strcmp(key,"state"))result=copy_text(buffer,capacity,snapshot->state.c_str());
        else if(!std::strcmp(key,"error"))result=copy_text(buffer,capacity,instance.overflow.load()?"Command queue full or value too long":snapshot->error.c_str());
        else if(!std::strcmp(key,"current_pad"))result=std::snprintf(buffer,capacity,"%d",settings.current+1);
        else if(!std::strcmp(key,"polyphony"))result=std::snprintf(buffer,capacity,"%d",settings.polyphony);
        else if(!std::strcmp(key,"default_root"))result=std::snprintf(buffer,capacity,"%d",settings.default_root);
        else if(!std::strcmp(key,"auto_root"))result=copy_text(buffer,capacity,settings.auto_root?"on":"off");
        else if(!std::strcmp(key,"slice_count"))result=std::snprintf(buffer,capacity,"%d",settings.slice_count);
        else if(!std::strcmp(key,"slice_analyze"))result=copy_text(buffer,capacity,settings.slice_analyze?"on":"off");
        else if(!std::strcmp(key,"pitched_mode"))result=copy_text(buffer,capacity,settings.pitched?"on":"off");
        else {
            int index=drum_engine?settings.current:0;const char *field=key;
            if(std::strncmp(key,"pad_",4)==0)field=key+4;
            else if(std::strlen(key)>4&&key[0]=='p'&&key[1]>='0'&&key[1]<='9'&&key[2]>='0'&&key[2]<='9'&&key[3]=='_'){
                index=(key[1]-'0')*10+(key[2]-'0')-1;field=key+4;
            }
            if(index>=0&&index<kPads) {
                const auto &pad=settings.pads[index];float value=0;bool numeric=true;
                if(!std::strcmp(field,"sample_path")){result=copy_text(buffer,capacity,pad.path.c_str());numeric=false;}
                else if(!std::strcmp(field,"warp")){result=copy_text(buffer,capacity,pad.warp.warp?"on":"off");numeric=false;}
                else if(!std::strcmp(field,"mode")){result=copy_text(buffer,capacity,pad.gate?"gate":"oneshot");numeric=false;}
                else if(!std::strcmp(field,"length_anchor")){result=copy_text(buffer,capacity,pad.warp.anchor==warp::LengthAnchor::Percent?"percent":"ms");numeric=false;}
                else if(!std::strcmp(field,"root_name")||!std::strcmp(field,"default_root_name")) {
                    static const char *names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    const int note=!std::strcmp(field,"root_name")?pad.warp.root_note:settings.default_root;
                    result=std::snprintf(buffer,capacity,"%s%d",names[note%12],note/12-2);numeric=false;
                }
                else if(!std::strcmp(field,"root_note"))value=pad.warp.root_note;
                else if(!std::strcmp(field,"trim_start"))value=pad.warp.trim_start;
                else if(!std::strcmp(field,"trim_end"))value=pad.warp.trim_end;
                else if(!std::strcmp(field,"length_pct"))value=pad.warp.length_percent;
                else if(!std::strcmp(field,"length_ms"))value=pad.warp.length_ms;
                else if(!std::strcmp(field,"grain_ms"))value=pad.warp.grain_ms;
                else if(!std::strcmp(field,"gain")||!std::strcmp(field,"vol"))value=pad.gain;
                else if(!std::strcmp(field,"pan"))value=pad.pan;
                else if(!std::strcmp(field,"tune"))value=pad.tune;
                else if(!std::strcmp(field,"transpose"))value=pad.transpose;
                else if(!std::strcmp(field,"fine_tune"))value=pad.fine;
                else if(!std::strcmp(field,"attack_ms"))value=pad.attack;
                else if(!std::strcmp(field,"release_ms")||!std::strcmp(field,"decay_ms"))value=pad.release;
                else if(!std::strcmp(field,"chance_pct"))value=pad.chance;
                else if(!std::strcmp(field,"choke_group"))value=pad.choke;
                else numeric=false;
                if(numeric)result=std::snprintf(buffer,capacity,"%.9g",value);
            }
        }
    }
    release(instance);return result;
}
static int get_error(void *pointer,char *buffer,int capacity){return get_param(pointer,"error",buffer,capacity);}
static void midi(void *pointer,const uint8_t *message,int length,int source) {
    if(!pointer||!message||length<3)return;auto &instance=*static_cast<Instance*>(pointer);
    const int type=message[0]&0xf0,channel=message[0]&15,note=message[1],velocity=message[2];
    if(note>127||velocity>127)return;
    if(source==0&&type==0xb0&&note==118){
        if(velocity&&!instance.sample_button)enqueue(instance,"record","on");
        instance.sample_button=velocity>0;return;
    }
    if(type==0xb0&&(note==120||note==123)){for(int index=0;index<kVoices;++index)stop_voice(instance,index);return;}
    if(type==0x80||(type==0x90&&!velocity)){
        for(auto &voice:instance.voices)if(voice.audio&&voice.note==note&&voice.channel==channel&&voice.gate)voice.releasing=true;
        return;
    }
    if(type!=0x90)return;
    auto *snapshot=acquire(instance);if(!snapshot){release(instance);return;}
    const auto &settings=snapshot->settings;
    const int pad_index=drum_engine?(settings.pitched?settings.current:note-36):0;
    if(pad_index<0||pad_index>=kPads){release(instance);return;}
    const auto &pad=settings.pads[pad_index];if(!pad.source){release(instance);return;}
    instance.random^=instance.random<<13;instance.random^=instance.random>>17;instance.random^=instance.random<<5;
    if(pad.chance<=0||double(instance.random)/4294967296.0*100>=pad.chance){release(instance);return;}
    const int pitch=drum_engine&&!settings.pitched?pad.warp.root_note:note;
    if(pad.choke)for(int index=0;index<kVoices;++index)
        if(instance.voices[index].audio&&instance.voices[index].choke==pad.choke)stop_voice(instance,index);
    int slot=0;for(int index=0;index<settings.polyphony;++index)if(!instance.voices[index].audio){slot=index;break;}
    stop_voice(instance,slot);instance.hazards[slot].store(snapshot);
    auto &voice=instance.voices[slot];voice=Voice{};
    const bool cached=pad.warp.warp&&bool(pad.notes[pitch]);
    voice.audio=cached?&pad.notes[pitch]->audio:&pad.source->audio;
    if(pad.warp.warp&&!cached)request_note(instance,pad_index,pitch,pad.generation);
    const auto &audio=*voice.audio;
    voice.position=cached?0:std::floor(pad.warp.trim_start*(audio.frames-1));
    voice.end=cached?audio.frames:std::ceil(pad.warp.trim_end*(audio.frames-1))+1;
    voice.increment=cached?1:std::pow(2.,(pitch-pad.warp.root_note+pad.tune)/12.)*audio.sample_rate/sample_rate();
    const float gain=velocity/127.f*pad.gain;
    voice.gain_left=gain*(drum_engine?std::sqrt((1-pad.pan)*.5f):1);
    voice.gain_right=gain*(drum_engine?std::sqrt((1+pad.pan)*.5f):1);
    voice.note=note;voice.channel=channel;voice.choke=pad.choke;voice.gate=pad.gate;
    voice.attack_step=pad.attack>0?1.f/(pad.attack*.001f*sample_rate()):1;
    voice.release_step=pad.release>0?1.f/(pad.release*.001f*sample_rate()):1;
    voice.decay_step=drum_engine&&!pad.gate&&pad.release>0?voice.release_step:0;
    voice.attacking=pad.attack>0;voice.envelope=voice.attacking?0:1;
    release(instance);
}
static void render(void *pointer,int16_t *output,int frames) {
    if(!pointer||!output||frames<=0)return;auto &instance=*static_cast<Instance*>(pointer);
    instance.capture_busy.store(true);
    if(instance.capture_state.load()==1&&host&&host->mapped_memory) {
        const size_t captured=instance.captured.load();
        const size_t limit=std::min(kCaptureSamples,size_t(sample_rate())*30*2);
        const size_t count=std::min(size_t(frames)*2,limit-captured);
        std::memcpy(instance.capture.data()+captured,host->mapped_memory+host->audio_in_offset,count*sizeof(int16_t));
        instance.captured.store(captured+count);
        if(captured+count==limit){int recording=1;instance.capture_state.compare_exchange_strong(recording,2);}
    }
    instance.capture_busy.store(false);
    // Sum in float and clip once, so cancellation is independent of voice order.
    for(int frame=0;frame<frames;++frame){float left=0,right=0;
        for(int index=0;index<kVoices;++index){auto &voice=instance.voices[index];if(!voice.audio)continue;
            if(voice.position>=voice.end){stop_voice(instance,index);continue;}
            if(voice.releasing){voice.envelope-=voice.release_step;if(voice.envelope<=0){stop_voice(instance,index);continue;}}
            else if(voice.attacking){voice.envelope=std::min(1.f,voice.envelope+voice.attack_step);if(voice.envelope>=1)voice.attacking=false;}
            else if(voice.decay_step>0){voice.envelope-=voice.decay_step;if(voice.envelope<=0){stop_voice(instance,index);continue;}}
            const auto &audio=*voice.audio;const int position=int(voice.position),next=std::min(position+1,audio.frames-1);
            const float fraction=float(voice.position-position);
            for(int channel=0;channel<2;++channel){const int source_channel=audio.channels==1?0:channel;
                const float begin=audio.data[size_t(position)*audio.channels+source_channel];
                const float end=audio.data[size_t(next)*audio.channels+source_channel];
                const float value=(begin+(end-begin)*fraction)*voice.envelope;
                if(channel==0)left+=value*voice.gain_left;else right+=value*voice.gain_right;
            }
            voice.position+=voice.increment;
        }
        output[frame*2]=int16_t(std::clamp(left,-1.f,1.f)*32767);
        output[frame*2+1]=int16_t(std::clamp(right,-1.f,1.f)*32767);
    }
}
static plugin_api_v2_t api{2,create,destroy,midi,set_param,get_param,get_error,render};
static plugin_api_v2_t *initialize(const host_api_v1_t *value,bool drums) {
    host=value;drum_engine=drums;
    if(!worker_started){pthread_attr_t attributes;pthread_attr_init(&attributes);
        pthread_attr_setinheritsched(&attributes,PTHREAD_EXPLICIT_SCHED);pthread_attr_setschedpolicy(&attributes,SCHED_OTHER);
        sched_param priority{};pthread_attr_setschedparam(&attributes,&priority);
        stopping.store(false);worker_started=pthread_create(&worker_thread,&attributes,worker,nullptr)==0;pthread_attr_destroy(&attributes);}
    return worker_started?&api:nullptr;
}
} // namespace sampler
