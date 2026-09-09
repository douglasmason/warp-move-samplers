#include "warp_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef WARP_USE_BUNGEE
#include <bungee/Bungee.h>
#endif

namespace warp {

static constexpr float PI = 3.14159265358979323846f;
static float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }
static int clampi(int x, int a, int b) { return std::max(a, std::min(b, x)); }

void PitchCache::invalidate() {
    for (auto &e : notes) { e.valid = false; e.midi_note = -1; e.audio = AudioBuffer{}; }
}

static uint16_t u16le(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
static uint32_t u32le(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static int32_t i24le(const uint8_t *p) { int32_t v=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16); if(v&0x800000)v|=~0xffffff; return v; }

bool read_wav(const std::string &path, AudioBuffer &out, std::string &err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "Could not open WAV: " + path; return false; }
    uint8_t h[12]; f.read((char*)h, 12);
    if (f.gcount()!=12 || std::memcmp(h,"RIFF",4) || std::memcmp(h+8,"WAVE",4)) { err="Not a RIFF/WAVE file"; return false; }
    uint16_t fmt=0,ch=0,bits=0,align=0; uint32_t sr=0,data_size=0; std::streampos data_pos{}; bool have_fmt=false,have_data=false;
    while (f) {
        uint8_t c[8]; f.read((char*)c,8); if(f.gcount()!=8) break;
        uint32_t n=u32le(c+4); auto pos=f.tellg();
        if (!std::memcmp(c,"fmt ",4)) {
            std::vector<uint8_t> b(std::max<uint32_t>(16,n)); f.read((char*)b.data(), n);
            if (n<16) { err="Corrupt fmt chunk"; return false; }
            fmt=u16le(b.data()); ch=u16le(b.data()+2); sr=u32le(b.data()+4); align=u16le(b.data()+12); bits=u16le(b.data()+14); have_fmt=true;
        } else if (!std::memcmp(c,"data",4)) { data_pos=pos; data_size=n; have_data=true; f.seekg(n, std::ios::cur); }
        else f.seekg(n, std::ios::cur);
        if (n&1) f.seekg(1,std::ios::cur);
    }
    if(!have_fmt||!have_data||ch<1||ch>2||align<1||sr<1000){err="Unsupported WAV layout";return false;}
    if(!((fmt==1&&(bits==8||bits==16||bits==24||bits==32))||(fmt==3&&bits==32))){err="Need PCM8/16/24/32 or float32 WAV";return false;}
    int frames=int(data_size/align); if(frames<1){err="Empty WAV";return false;}
    f.clear(); f.seekg(data_pos); std::vector<uint8_t> raw(data_size); f.read((char*)raw.data(),data_size); if((uint32_t)f.gcount()!=data_size){err="Short WAV data";return false;}
    out=AudioBuffer{}; out.frames=frames; out.channels=ch; out.sample_rate=sr; out.data.resize((size_t)frames*ch);
    int bps=bits/8;
    for(int i=0;i<frames;i++) for(int c=0;c<ch;c++) {
        const uint8_t *p=raw.data()+size_t(i)*align+c*bps; float v=0;
        if(fmt==3){ std::memcpy(&v,p,4); }
        else if(bits==8) v=((int)p[0]-128)/128.f;
        else if(bits==16) v=(int16_t)u16le(p)/32768.f;
        else if(bits==24) v=i24le(p)/8388608.f;
        else v=(int32_t)u32le(p)/2147483648.f;
        out.data[size_t(i)*ch+c]=clampf(v,-1,1);
    }
    return true;
}

bool write_wav16(const std::string &path, const AudioBuffer &in, std::string &err) {
    if(in.frames<=0||in.channels<1||in.channels>2||in.sample_rate<1000){err="Invalid audio buffer";return false;}
    std::ofstream f(path,std::ios::binary); if(!f){err="Could not create WAV: "+path;return false;}
    uint32_t data_bytes=uint32_t(in.frames*in.channels*2); uint32_t riff_size=36+data_bytes;
    auto w16=[&](uint16_t v){ char b[2]={char(v&255),char((v>>8)&255)}; f.write(b,2);};
    auto w32=[&](uint32_t v){ char b[4]={char(v&255),char((v>>8)&255),char((v>>16)&255),char((v>>24)&255)}; f.write(b,4);};
    f.write("RIFF",4);w32(riff_size);f.write("WAVEfmt ",8);w32(16);w16(1);w16(in.channels);w32(in.sample_rate);w32(in.sample_rate*in.channels*2);w16(in.channels*2);w16(16);f.write("data",4);w32(data_bytes);
    for(float x:in.data){ int v=int(std::lrint(clampf(x,-1,1)*32767.f)); int16_t s=(int16_t)clampi(v,-32768,32767); w16((uint16_t)s); }
    return bool(f);
}

AudioBuffer trim_audio(const AudioBuffer &src,float a,float b){
    AudioBuffer out; if(src.frames<=0)return out; a=clampf(a,0,1);b=clampf(b,0,1);if(b<a)std::swap(a,b); int i0=clampi(int(std::floor(a*(src.frames-1))),0,src.frames-1); int i1=clampi(int(std::ceil(b*(src.frames-1)))+1,i0+1,src.frames);
    out.frames=i1-i0;out.channels=src.channels;out.sample_rate=src.sample_rate;out.data.resize(size_t(out.frames)*out.channels);std::copy(src.data.begin()+size_t(i0)*src.channels,src.data.begin()+size_t(i1)*src.channels,out.data.begin());return out;
}
float duration_ms(const AudioBuffer &src){return src.sample_rate>0?1000.f*src.frames/src.sample_rate:0.f;}
void reconcile_length(SampleWarpState &s,float trimmed_ms,LengthAnchor changed){ s.anchor=changed; if(trimmed_ms<=0){s.length_ms=0;return;} if(changed==LengthAnchor::Percent){s.length_percent=clampf(s.length_percent,1.f,800.f);s.length_ms=trimmed_ms*s.length_percent/100.f;} else {s.length_ms=clampf(s.length_ms,1.f,600000.f);s.length_percent=100.f*s.length_ms/trimmed_ms;} }
float midi_to_hz(float midi){return 440.f*std::pow(2.f,(midi-69.f)/12.f);}

PitchEstimate detect_pitch(const AudioBuffer &src,float min_hz,float max_hz){
    PitchEstimate r; if(src.frames<128||src.sample_rate<1000)return r; int ds=std::max(1,src.sample_rate/11025); int n=std::min(src.frames/ds,22050); if(n<128)return r; std::vector<float>x(n); double mean=0; for(int i=0;i<n;i++){float s=0;for(int c=0;c<src.channels;c++)s+=src.data[size_t(i*ds)*src.channels+c];x[i]=s/src.channels;mean+=x[i];} mean/=n; double energy=0;for(float &v:x){v-=float(mean);energy+=v*v;} if(energy/n<1e-8)return r;
    float sr=float(src.sample_rate)/ds; int minlag=std::max(2,int(sr/max_hz)); int maxlag=std::min(n/3,int(sr/min_hz)); float best=-1;int bestlag=0;
    for(int lag=minlag;lag<=maxlag;lag++){ double num=0,a=0,b=0; int m=n-lag; for(int i=0;i<m;i++){double u=x[i],v=x[i+lag];num+=u*v;a+=u*u;b+=v*v;} float c=(a>0&&b>0)?float(num/std::sqrt(a*b)):0; if(c>best){best=c;bestlag=lag;} }
    if(bestlag<=0||best<0.45f)return r; float lagf=float(bestlag); if(bestlag>minlag&&bestlag<maxlag){ auto corr=[&](int lag){double num=0,a=0,b=0;int m=n-lag;for(int i=0;i<m;i++){double u=x[i],v=x[i+lag];num+=u*v;a+=u*u;b+=v*v;}return float(num/std::sqrt(std::max(1e-20,a*b)));}; float y1=corr(bestlag-1),y2=best,y3=corr(bestlag+1); float den=y1-2*y2+y3; if(std::fabs(den)>1e-6f)lagf+=0.5f*(y1-y3)/den; }
    float hz=sr/lagf; float midi=69.f+12.f*std::log2(hz/440.f); int nm=int(std::lround(midi)); r.valid=true;r.frequency=hz;r.midi=midi;r.nearest_midi=clampi(nm,0,127);r.cents=(midi-nm)*100.f;r.confidence=best;return r;
}

static int next_pow2(int x){int p=1;while(p<x&&p<16384)p<<=1;return p;}
static void fft(std::vector<std::complex<float>>&a,bool inv){int n=a.size();for(int i=1,j=0;i<n;i++){int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}for(int len=2;len<=n;len<<=1){float ang=2*PI/len*(inv?1:-1);std::complex<float>wlen(std::cos(ang),std::sin(ang));for(int i=0;i<n;i+=len){std::complex<float>w(1);for(int j=0;j<len/2;j++){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}if(inv)for(auto&x:a)x/=float(n);}

static std::vector<float> resample_linear(const std::vector<float>&x,int out_n){std::vector<float>o(std::max(0,out_n));if(x.empty()||out_n<=0)return o;if(x.size()==1){std::fill(o.begin(),o.end(),x[0]);return o;}if(out_n==1){o[0]=x[0];return o;}double scale=double(x.size()-1)/(out_n-1);for(int i=0;i<out_n;i++){double p=i*scale;int k=int(p);double f=p-k;if(k>=int(x.size())-1)o[i]=x.back();else o[i]=float(x[k]*(1-f)+x[k+1]*f);}return o;}

static std::vector<float> loop_or_trim_preserving_pitch(const std::vector<float>&x,int target){
    std::vector<float> out(std::max(0,target),0.0f);
    if(x.empty()||target<=0)return out;
    if(target<=int(x.size())){
        std::copy_n(x.begin(),target,out.begin());
        return out;
    }
    for(int i=0;i<target;i++)out[i]=x[size_t(i)%x.size()];
    return out;
}

static std::vector<float> pv_stretch(const std::vector<float>&x,int target,float grain_ms,int sr){
    if(target<=0||x.empty())return{}; if(x.size()<128)return resample_linear(x,target); float ratio=float(target)/x.size(); int N=next_pow2(clampi(int(grain_ms*0.001f*sr),256,8192)); if(N>int(x.size()))N=next_pow2(std::max(128,int(x.size()/2)));N=clampi(N,128,8192);int Ha=std::max(16,N/4);double Hs=Ha*ratio; int frames=1+std::max(1, int(std::ceil(double(x.size()) / double(Ha)))); if(frames<2)return resample_linear(x,target);
    int alloc=target+N*2;std::vector<float>out(alloc,0),norm(alloc,0),win(N);for(int i=0;i<N;i++)win[i]=0.5f-0.5f*std::cos(2*PI*i/(N-1));
    std::vector<float>prev(N/2+1,0),syn(N/2+1,0);std::vector<std::complex<float>>a(N);bool first=true;
    for(int m=0;m<frames;m++){int inpos=m*Ha;for(int i=0;i<N;i++){float s=(inpos+i<int(x.size()))?x[inpos+i]:0;a[i]=std::complex<float>(s*win[i],0);}fft(a,false);for(int k=0;k<=N/2;k++){float ph=std::atan2(a[k].imag(),a[k].real()),mag=std::abs(a[k]),omega=2*PI*k/N;if(first){syn[k]=ph;}else{float d=ph-prev[k]-omega*Ha;while(d>PI)d-=2*PI;while(d<-PI)d+=2*PI;float tf=omega+d/Ha;syn[k]+=tf*Hs;}prev[k]=ph;a[k]=std::polar(mag,syn[k]);if(k>0&&k<N/2)a[N-k]=std::conj(a[k]);}first=false;fft(a,true);double op=m*Hs;int base=int(std::floor(op));float frac=float(op-base);for(int i=0;i<N;i++){float v=a[i].real()*win[i];float w=win[i]*win[i];int j=base+i;if(j>=0&&j<alloc){out[j]+=v*(1-frac);norm[j]+=w*(1-frac);}if(frac>0&&j+1<alloc){out[j+1]+=v*frac;norm[j+1]+=w*frac;}}}
    for(int i=0;i<alloc;i++)if(norm[i]>1e-5f)out[i]/=norm[i];out.resize(target);return out;
}

#ifdef WARP_USE_BUNGEE
static AudioBuffer render_bungee(const AudioBuffer &src, float semitones, int target_frames, float grain_ms) {
    AudioBuffer out;
    if (src.frames <= 1 || target_frames <= 0) return out;
    out.frames = target_frames; out.channels = src.channels; out.sample_rate = src.sample_rate;
    out.data.assign(size_t(target_frames) * src.channels, 0.0f);

    int hop_adjust = 0;
    if (grain_ms < 25.0f) hop_adjust = -1;
    else if (grain_ms > 90.0f) hop_adjust = +1;

    Bungee::Stretcher<Bungee::Basic> stretcher(
        Bungee::SampleRates{src.sample_rate, src.sample_rate}, src.channels, hop_adjust);
    int max_grain = stretcher.maxInputFrameCount();
    std::vector<float> grain(size_t(max_grain) * src.channels, 0.0f);
    Bungee::Request req{};
    req.position = 0.0;
    req.speed = double(src.frames) / double(target_frames);
    req.pitch = std::pow(2.0, double(semitones) / 12.0);
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
    stretcher.preroll(req);

    int written = 0;
    int safety = std::max(target_frames * 2, 10000);
    while (written < target_frames && req.position < double(src.frames) && safety-- > 0) {
        Bungee::InputChunk chunk = stretcher.specifyGrain(req);
        int len = chunk.end - chunk.begin;
        std::fill(grain.begin(), grain.end(), 0.0f);
        for (int i=0;i<len && i<max_grain;i++) {
            int si = chunk.begin + i;
            if (si < 0 || si >= src.frames) continue;
            for (int c=0;c<src.channels;c++) grain[size_t(c)*max_grain+i] = src.data[size_t(si)*src.channels+c];
        }
        int mute_head = std::max(0, -chunk.begin);
        int mute_tail = std::max(0, chunk.end - src.frames);
        stretcher.analyseGrain(grain.data(), max_grain, mute_head, mute_tail);
        Bungee::OutputChunk oc{}; stretcher.synthesiseGrain(oc);
        int copy = std::min(oc.frameCount, target_frames-written);
        for (int i=0;i<copy;i++) for(int c=0;c<src.channels;c++)
            out.data[size_t(written+i)*src.channels+c] = oc.data[i + c*oc.channelStride];
        written += copy;
        stretcher.next(req); req.reset = false;
    }
    // Bungee may return a few frames short at the tail. Keep the contract exact and pad silence.
    return out;
}
#endif

AudioBuffer render_constant_duration(const AudioBuffer&src,float semitones,int target_frames,float grain_ms){
#ifdef WARP_USE_BUNGEE
    return render_bungee(src,semitones,target_frames,grain_ms);
#else
    AudioBuffer out; if(src.frames<=1||target_frames<=0)return out;
    out.frames=target_frames;out.channels=src.channels;out.sample_rate=src.sample_rate;out.data.resize(size_t(target_frames)*src.channels);
    const double p=std::pow(2.0,double(semitones)/12.0);
    for(int c=0;c<src.channels;c++){
        std::vector<float>x(src.frames);for(int i=0;i<src.frames;i++)x[i]=src.data[size_t(i)*src.channels+c];
        int pitched_n=std::max(2,int(std::lround(src.frames/p)));
        std::vector<float>pitched(pitched_n);
        for(int i=0;i<pitched_n;i++){double pos=i*p;int k=int(std::floor(pos));double f=pos-k;if(k>=src.frames-1)pitched[i]=x.back();else pitched[i]=float(x[k]*(1-f)+x[k+1]*f);}
        auto y=loop_or_trim_preserving_pitch(pitched,target_frames);
        for(int i=0;i<target_frames;i++)out.data[size_t(i)*src.channels+c]=(i<int(y.size()))?y[i]:0.0f;
    }
    return out;
#endif
}

std::string midi_note_name_ableton(int midi){static const char*nn[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};midi=clampi(midi,0,127);int oct=midi/12-2;std::ostringstream s;s<<nn[midi%12]<<oct;return s.str();}
int parse_midi_note(const std::string&s,int fb){if(s.empty())return fb;char*e=nullptr;long n=std::strtol(s.c_str(),&e,10);if(e&&*e==' ')return clampi(int(n),0,127);static const char*nn[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};std::string u=s;for(char&c:u)if(c=='b'){};int pc=-1,cons=0;for(int i=0;i<12;i++){std::string q=nn[i];if(u.rfind(q,0)==0&&int(q.size())>cons){pc=i;cons=q.size();}}if(pc<0)return fb;try{int oct=std::stoi(u.substr(cons));return clampi((oct+2)*12+pc,0,127);}catch(...){return fb;}}

void CachedSample::set_source(AudioBuffer a){source=std::move(a);state.trim_start=0;state.trim_end=1;if(state.anchor==LengthAnchor::Percent){state.length_percent=100;}reconcile_length(state,trimmed_ms(),state.anchor);invalidate();}
float CachedSample::trimmed_ms()const{return duration_ms(trim_audio(source,state.trim_start,state.trim_end));}
int CachedSample::target_frames(int output_sr)const{return std::max(1,int(std::lround(state.length_ms*0.001f*output_sr)));}
void CachedSample::invalidate(){cache.invalidate();}
bool CachedSample::is_cached(int note) const {
    note=clampi(note,0,127);
    return state.warp && cache.notes[note].valid;
}
void CachedSample::prewarm(int low_note,int high_note,int output_sr){
    if(!state.warp||source.frames<=0)return;
    int lo=clampi(std::min(low_note,high_note),0,127);
    int hi=clampi(std::max(low_note,high_note),0,127);
    for(int note=lo;note<=hi;note++)get_or_render(note,output_sr);
}
const AudioBuffer* CachedSample::get_or_render(int note,int output_sr){note=clampi(note,0,127);if(!state.warp)return nullptr;auto&e=cache.notes[note];if(e.valid)return &e.audio;AudioBuffer t=trim_audio(source,state.trim_start,state.trim_end);if(t.frames<=0)return nullptr;if(t.sample_rate!=output_sr){ // normalize source sample rate first without changing pitch/time materially
        int n=std::max(2,int(std::lround(double(t.frames)*output_sr/t.sample_rate)));AudioBuffer r;r.frames=n;r.channels=t.channels;r.sample_rate=output_sr;r.data.resize(size_t(n)*r.channels);for(int c=0;c<r.channels;c++){std::vector<float>x(t.frames);for(int i=0;i<t.frames;i++)x[i]=t.data[size_t(i)*t.channels+c];auto y=resample_linear(x,n);for(int i=0;i<n;i++)r.data[size_t(i)*r.channels+c]=y[i];}t=std::move(r);}
    float semis=float(note-state.root_note+transpose)+fine_cents/100.f;e.audio=render_constant_duration(t,semis,target_frames(output_sr),state.grain_ms);e.valid=true;e.midi_note=note;return &e.audio;}

} // namespace warp
