# Lightweight ABI smoke test. It deliberately tests ingest/state/slicing, not fallback pitch quality.
import ctypes, math, pathlib, wave, struct
R=pathlib.Path(__file__).parents[1]; SR=44100
wav=R/'tests'/'a440_test.wav'
with wave.open(str(wav),'wb') as w:
    w.setnchannels(1);w.setsampwidth(2);w.setframerate(SR);w.writeframes(b''.join(struct.pack('<h',int(12000*math.sin(2*math.pi*440*i/SR))) for i in range(SR//2)))
LOG=ctypes.CFUNCTYPE(None,ctypes.c_char_p); MIDI=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.POINTER(ctypes.c_uint8),ctypes.c_int)
@LOG
def log(_):pass
@MIDI
def midi(_a,_b):return 0
class H(ctypes.Structure):_fields_=[('api_version',ctypes.c_uint32),('sample_rate',ctypes.c_int),('frames_per_block',ctypes.c_int),('mapped_memory',ctypes.POINTER(ctypes.c_uint8)),('audio_out_offset',ctypes.c_int),('audio_in_offset',ctypes.c_int),('log',LOG),('midi_send_internal',MIDI),('midi_send_external',MIDI)]
CREATE=ctypes.CFUNCTYPE(ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p);DEST=ctypes.CFUNCTYPE(None,ctypes.c_void_p);ON=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint8),ctypes.c_int,ctypes.c_int);SET=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p);GET=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int);ERR=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int);REN=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.POINTER(ctypes.c_int16),ctypes.c_int)
class A(ctypes.Structure):_fields_=[('api_version',ctypes.c_uint32),('create_instance',CREATE),('destroy_instance',DEST),('on_midi',ON),('set_param',SET),('get_param',GET),('get_error',ERR),('render_block',REN)]
mem=(ctypes.c_uint8*512)();host=H(2,SR,128,mem,0,0,log,midi,midi)
def api(path):
    l=ctypes.CDLL(str(path));f=l.move_plugin_init_v2;f.argtypes=[ctypes.POINTER(H)];f.restype=ctypes.POINTER(A);return l,f(ctypes.byref(host)).contents
def get(a,i,k):
    size=16384 if k in ('chain_params','ui_hierarchy','state') else 512
    b=ctypes.create_string_buffer(size)
    assert a.get_param(i,k.encode(),b,size)>=0
    return b.value.decode()
l,a=api(R/'build/host/melodic_dsp.so');i=a.create_instance(str(R/'modules/warpmrsample').encode(),b'{}');a.set_param(i,b'sample_path',str(wav).encode());assert get(a,i,'root_note')=='69';import json as _json;assert len(_json.loads(get(a,i,'chain_params')))>=10;assert 'levels' in _json.loads(get(a,i,'ui_hierarchy'));assert 'sample_path' in _json.loads(get(a,i,'state'));a.set_param(i,b'length_pct',b'150');assert abs(float(get(a,i,'length_ms'))-750)<1;a.set_param(i,b'length_ms',b'250');assert abs(float(get(a,i,'length_pct'))-50)<1;a.destroy_instance(i);print('melodic ABI ok')
l2,d=api(R/'build/host/drums_dsp.so');i=d.create_instance(str(R/'modules/warpmrdrums').encode(),b'{}');d.set_param(i,b'p01_sample_path',str(wav).encode());d.set_param(i,b'slice_count',b'2');d.set_param(i,b'slice',b'1');
for p in (1,2):d.set_param(i,b'current_pad',str(p).encode());assert get(d,i,'pad_root_note')=='69';assert abs(float(get(d,i,'pad_grain_ms'))-45)<.1;assert get(d,i,'pad_warp')=='on'
d.destroy_instance(i);print('drum ABI/slice ok')
