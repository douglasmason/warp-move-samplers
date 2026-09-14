#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
static thread_local bool realtime_test=false;
static std::atomic<int> realtime_allocations{0};
void *operator new(std::size_t size) {if(realtime_test)++realtime_allocations;if(void *pointer=std::malloc(size?size:1))return pointer;throw std::bad_alloc();}
void *operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void *pointer) noexcept {if(realtime_test)++realtime_allocations;std::free(pointer);}
void operator delete[](void *pointer) noexcept {::operator delete(pointer);}
void operator delete(void *pointer,std::size_t) noexcept {::operator delete(pointer);}
void operator delete[](void *pointer,std::size_t) noexcept {::operator delete(pointer);}
#ifdef TEST_DRUMS
#include "../src/drums/plugin.cpp"
#else
#include "../src/melodic/plugin.cpp"
#endif
struct RealtimeScope {RealtimeScope(){realtime_test=true;}~RealtimeScope(){realtime_test=false;}};
static std::string read(plugin_api_v2_t *api,void *instance,const char *key) {
    char result[32768];int length;
    {RealtimeScope scope;length=api->get_param(instance,key,result,sizeof(result));}
    assert(length>=0&&length<int(sizeof(result)));return result;
}
static void settle(plugin_api_v2_t *api,void *instance) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(true){auto status=read(api,instance,"status");if(status!="Initializing"&&status!="Preparing")break;
        assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    const auto error=read(api,instance,"error");if(!error.empty())std::cerr<<error<<"\n";assert(error.empty());
}
static void set(plugin_api_v2_t *api,void *instance,const char *key,const char *value){RealtimeScope scope;api->set_param(instance,key,value);}
int main() {
    uint8_t input[512]{};
    host_api_v1_t host{};host.sample_rate=44100;host.frames_per_block=128;host.mapped_memory=input;
    auto *api=move_plugin_init_v2(&host);assert(api);
    host.sample_rate=1000;assert(sampler::sample_rate()==44100);host.sample_rate=44100;
    int scheduling_policy; sched_param scheduling_parameters{};
    assert(pthread_getschedparam(sampler::worker_thread,&scheduling_policy,&scheduling_parameters)==0);
    assert(scheduling_policy==SCHED_OTHER);
    const auto directory=std::filesystem::temp_directory_path()/
        ("warp-regression-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    warp::AudioBuffer audio;audio.frames=4410;audio.data.resize(audio.frames);
    for(int frame=0;frame<audio.frames;++frame)audio.data[frame]=.5f*std::sin(2*3.141592653589793*440*frame/44100);
    std::string error;const auto path=(directory/"tone.wav").string();assert(warp::write_wav16(path,audio,error));
    void *instance;
    {RealtimeScope scope;instance=api->create_instance(directory.c_str(),"{\"auto_root\":\"off\",\"default_root\":69}");}
    assert(instance);settle(api,instance);
#ifdef TEST_DRUMS
    const char *sample_key="p01_sample_path",*length_key="pad_length_pct";
    set(api,instance,"pad_mode","gate");set(api,instance,"pad_decay_ms","0");
    uint8_t start[]={0x90,36,100},stop[]={0x80,36,0};
#else
    const char *sample_key="sample_path",*length_key="length_pct";
    set(api,instance,"release_ms","0");set(api,instance,"attack_ms","0");
    uint8_t start[]={0x90,69,100},stop[]={0x80,69,0};
#endif
    set(api,instance,sample_key,path.c_str());settle(api,instance);
    int16_t output[256]{};
    {RealtimeScope scope;api->on_midi(instance,start,3,2);api->render_block(instance,output,128);}
    assert(std::any_of(std::begin(output),std::end(output),[](int16_t value){return value!=0;}));
    // Replace the cache while a note is held. That note retains its original buffer.
    set(api,instance,length_key,"200");settle(api,instance);
    {RealtimeScope scope;api->render_block(instance,output,128);}
    assert(std::any_of(std::begin(output),std::end(output),[](int16_t value){return value!=0;}));
    {RealtimeScope scope;api->on_midi(instance,stop,3,2);api->render_block(instance,output,128);}
    assert(std::all_of(std::begin(output),std::end(output),[](int16_t value){return value==0;}));
#ifdef TEST_DRUMS
    set(api,instance,"pad_tune","12.5");settle(api,instance);
    int crossings=0;int16_t previous=0;
    {RealtimeScope scope;api->on_midi(instance,start,3,2);}
    for(int block=0;block<30;++block){RealtimeScope scope;api->render_block(instance,output,128);
        for(int frame=0;frame<128;++frame){if(previous<=0&&output[frame*2]>0)++crossings;previous=output[frame*2];}}
    assert(crossings>70&&crossings<85);
#endif
    // A miss outside the prepared range plays immediately, and queues preparation.
    uint8_t high[]={0x90,100,100},panic[]={0xb0,123,0};
#ifdef TEST_DRUMS
    set(api,instance,"pitched_mode","on");settle(api,instance);
#endif
    {RealtimeScope scope;api->on_midi(instance,panic,3,2);api->on_midi(instance,high,3,2);api->render_block(instance,output,128);}
    assert(std::any_of(std::begin(output),std::end(output),[](int16_t value){return value!=0;}));
    {RealtimeScope scope;api->on_midi(instance,panic,3,2);api->render_block(instance,output,128);}
    assert(std::all_of(std::begin(output),std::end(output),[](int16_t value){return value==0;}));
    settle(api,instance);
    assert(std::stoull(read(api,instance,"audio_bytes"))<=sampler::kAudioBudget);
    // Recordings are saved on the worker, and pad switching cannot redirect capture.
    set(api,instance,"record","on");settle(api,instance);
#ifdef TEST_DRUMS
    set(api,instance,"current_pad","2");settle(api,instance);
#endif
    for(int block=0;block<10;++block){RealtimeScope scope;api->render_block(instance,output,128);}
    set(api,instance,"record","on");settle(api,instance);
    const auto first_recording=read(api,instance,sample_key);assert(first_recording!=path&&std::filesystem::exists(first_recording));
#ifdef TEST_DRUMS
    set(api,instance,"current_pad","1");settle(api,instance);
#endif
    set(api,instance,"record","on");settle(api,instance);
    for(int block=0;block<10;++block){RealtimeScope scope;api->render_block(instance,output,128);}
    set(api,instance,"record","on");settle(api,instance);
    const auto second_recording=read(api,instance,sample_key);assert(first_recording!=second_recording&&std::filesystem::exists(first_recording));
#ifdef TEST_DRUMS
    set(api,instance,"pad_warp","off");
#else
    set(api,instance,"warp","off");
#endif
    settle(api,instance);set(api,instance,"record","on");settle(api,instance);
    for(int block=0;block<(44100*30+127)/128;++block){RealtimeScope scope;api->render_block(instance,output,128);}
    settle(api,instance);
    assert(read(api,instance,"recording")=="off");
    assert(sampler::instances[0].captured.load()==44100*30*2);
    assert(read(api,instance,sample_key)!=second_recording);
    {RealtimeScope scope;api->destroy_instance(instance);}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(sampler::instances[0].life.load()!=0){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    assert(sampler::audio_bytes_readout.load()==0);
    assert(realtime_allocations.load()==0);
    std::filesystem::remove_all(directory);
    std::cout<<"Async playback, gate, live edit, capture, lifecycle, and zero-allocation callback checks passed\n";
}
