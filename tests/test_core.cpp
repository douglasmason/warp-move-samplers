#include "warp_core.h"
#include <cmath>
#include <cstdio>
using namespace warp;
int main(){
 AudioBuffer a;a.frames=44100;a.channels=1;a.sample_rate=44100;a.data.resize(a.frames);for(int i=0;i<a.frames;i++)a.data[i]=0.5f*std::sin(2*3.141592653589793*440*i/a.sample_rate);
 auto p=detect_pitch(a);if(!p.valid||std::fabs(p.frequency-440)>3){std::fprintf(stderr,"pitch detect failed %.2f\n",p.frequency);return 1;}
 SampleWarpState s;s.length_percent=150;reconcile_length(s,1000,LengthAnchor::Percent);if(std::fabs(s.length_ms-1500)>.1)return 2;s.length_ms=500;reconcile_length(s,1000,LengthAnchor::Milliseconds);if(std::fabs(s.length_percent-50)>.1)return 3;
 auto y=render_constant_duration(a,0,22050,45);if(y.frames!=22050)return 4;
 std::printf("core ok: root=%s confidence=%.3f duration=%d\n",midi_note_name_ableton(p.nearest_midi).c_str(),p.confidence,y.frames);return 0;
}
