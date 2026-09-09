#include "warp_core.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace warp;
static float estimate_zero(const AudioBuffer&a){if(a.frames<4)return 0;int c=0;for(int i=1;i<a.frames;i++){float x=a.data[(i-1)*a.channels],y=a.data[i*a.channels];if(x<=0&&y>0)c++;}return c*a.sample_rate/float(a.frames);}
int main(){AudioBuffer a;a.frames=44100;a.channels=1;a.sample_rate=44100;a.data.resize(a.frames);for(int i=0;i<a.frames;i++)a.data[i]=0.6f*std::sin(2*M_PI*440.0*i/a.sample_rate);
 auto p=detect_pitch(a);printf("detect %.2fHz midi %.2f conf %.3f\n",p.frequency,p.midi,p.confidence);if(!p.valid||std::fabs(p.frequency-440)>3)return 1;
 for(float st: {0.f,12.f,-12.f}){auto y=render_constant_duration(a,st,44100,45);float f=estimate_zero(y);printf("st %.0f frames %d f %.2f\n",st,y.frames,f);float want=440*std::pow(2.f,st/12.f);if(y.frames!=44100||std::fabs(f-want)>8)return 2;}
 SampleWarpState s;s.length_percent=150;reconcile_length(s,1000,LengthAnchor::Percent);if(std::fabs(s.length_ms-1500)>0.1)return 3;s.length_ms=500;reconcile_length(s,1000,LengthAnchor::Milliseconds);if(std::fabs(s.length_percent-50)>0.1)return 4;
 CachedSample cached;cached.state.root_note=69;cached.state.warp=true;cached.state.length_percent=100;cached.set_source(a);cached.prewarm(57,81,44100);for(int note=57;note<=81;note++)if(!cached.is_cached(note))return 5;if(cached.is_cached(56)||cached.is_cached(82))return 6;printf("prewarm ok 57..81\n");
 printf("ok\n");return 0;}
