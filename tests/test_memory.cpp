#define SAMPLER_AUDIO_BUDGET (256 * 1024)
#include "../src/melodic/plugin.cpp"
#include <cassert>
#include <iostream>
static std::string get(plugin_api_v2_t *api,void *instance,const char *key){char value[32768];assert(api->get_param(instance,key,value,sizeof(value))>=0);return value;}
static void wait(plugin_api_v2_t *api,void *instance){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(true){const auto status=get(api,instance,"status");if(status!="Preparing"&&status!="Initializing")return;
        assert(std::chrono::steady_clock::now()<deadline);assert(sampler::audio_bytes_readout.load()<=sampler::kAudioBudget);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
int main(){host_api_v1_t host{};host.sample_rate=44100;auto *api=move_plugin_init_v2(&host);
    const auto directory=std::filesystem::temp_directory_path()/("warp-memory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    warp::AudioBuffer audio;audio.frames=4410;audio.data.resize(4410,.3f);std::string error;const auto path=(directory/"source.wav").string();assert(warp::write_wav16(path,audio,error));
    void *first=api->create_instance(directory.c_str(),"{\"auto_root\":\"off\"}");assert(first);wait(api,first);
    api->set_param(first,"sample_path",path.c_str());wait(api,first);assert(get(api,first,"error").empty());
    void *second=api->create_instance(directory.c_str(),"{\"auto_root\":\"off\"}");assert(second);wait(api,second);
    // The second instance must be able to evict the first instance's unused cache.
    api->set_param(second,"sample_path",path.c_str());wait(api,second);assert(get(api,second,"sample_path")==path);assert(get(api,second,"error").empty());
    uint8_t note[]={0x90,60,100},panic[]={0xb0,123,0};int16_t output[256];api->on_midi(second,note,3,2);api->render_block(second,output,128);
    // Pinned snapshots can prevent more rendering. The limit must be respected,
    // and held audio must remain valid even when the new cache cannot fit.
    api->set_param(second,"length_ms","1000");wait(api,second);assert(sampler::audio_bytes_readout.load()<=sampler::kAudioBudget);
    api->render_block(second,output,128);assert(output[100]!=0);
    api->on_midi(second,panic,3,2);api->set_param(second,"sample_path","");wait(api,second);
    api->set_param(first,"sample_path","");wait(api,first);
    api->destroy_instance(first);api->destroy_instance(second);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(sampler::audio_bytes_readout.load()!=0){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    std::filesystem::remove_all(directory);std::cout<<"Global cache budget, cross-instance eviction, and pinned audio checks passed\n";
}
