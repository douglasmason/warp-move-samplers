"""Exercise asynchronous loading and saved state through the public plugin ABI."""
import ctypes
import json
import math
import pathlib
import struct
import tempfile
import time
import wave
from typing import Any

R: pathlib.Path = pathlib.Path(__file__).parents[1]
SR: int = 44100
LOG: Any = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
MIDI: Any = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int)

@LOG
def log(message: bytes) -> None:
    """Discard plugin logging in tests."""

@MIDI
def midi(message: Any, length: int) -> int:
    """Provide an unused host MIDI output stub."""
    return 0

class Host(ctypes.Structure):
    """Stable prefix consumed by the sampler host ABI."""
    _fields_ = [('api_version', ctypes.c_uint32), ('sample_rate', ctypes.c_int), ('frames_per_block', ctypes.c_int), ('mapped_memory', ctypes.POINTER(ctypes.c_uint8)), ('audio_out_offset', ctypes.c_int), ('audio_in_offset', ctypes.c_int), ('log', LOG), ('midi_send_internal', MIDI), ('midi_send_external', MIDI)]

CREATE: Any = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p)
DESTROY: Any = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
ON_MIDI: Any = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int)
SET: Any = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p)
GET: Any = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int)
ERROR: Any = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int)
RENDER: Any = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int)

class API(ctypes.Structure):
    """Schwung v2 plugin entry points."""
    _fields_ = [('api_version', ctypes.c_uint32), ('create_instance', CREATE), ('destroy_instance', DESTROY), ('on_midi', ON_MIDI), ('set_param', SET), ('get_param', GET), ('get_error', ERROR), ('render_block', RENDER)]

memory: Any = (ctypes.c_uint8 * 512)()
host: Host = Host(2, SR, 128, memory, 0, 0, log, midi)

def api(path: pathlib.Path) -> tuple[Any, API]:
    """Load one DSP and retain its library handle."""
    library: Any = ctypes.CDLL(str(path))
    initialize: Any = library.move_plugin_init_v2
    initialize.argtypes = [ctypes.POINTER(Host)]
    initialize.restype = ctypes.POINTER(API)
    return library, initialize(ctypes.byref(host)).contents

def get(plugin: API, instance: Any, key: str) -> str:
    """Read a complete parameter without silently accepting truncation."""
    buffer: Any = ctypes.create_string_buffer(32768)
    length: int = plugin.get_param(instance, key.encode(), buffer, len(buffer))
    assert 0 <= length < len(buffer), (key, length)
    return buffer.value.decode()

def settle(plugin: API, instance: Any) -> None:
    """Wait for worker preparation with a bounded timeout."""
    deadline: float = time.monotonic() + 30
    while get(plugin, instance, 'status') in {'Initializing', 'Preparing'}:
        assert time.monotonic() < deadline, 'worker timeout'
        time.sleep(.005)
    error: str = get(plugin, instance, 'error')
    assert not error, error

def main() -> None:
    """Round-trip settings and validate both instrument families."""
    with tempfile.TemporaryDirectory(prefix='warp-abi-') as directory:
        root: pathlib.Path = pathlib.Path(directory)
        sample_path: pathlib.Path = root / 'a"440\\tone.wav'
        with wave.open(str(sample_path), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(SR)
            output.writeframes(b''.join(struct.pack('<h', int(12000*math.sin(2*math.pi*440*frame/SR))) for frame in range(SR//2)))
        for engine in ('melodic', 'drums'):
            library: Any
            plugin: API
            library, plugin = api(R / f'build/host/{engine}_dsp.so')
            instance: Any = plugin.create_instance(str(root).encode(), b'{"auto_root":"off","default_root":69}')
            assert instance
            settle(plugin, instance)
            prefix: str = 'p01_' if engine == 'drums' else ''
            plugin.set_param(instance, (prefix+'sample_path').encode(), str(sample_path).encode())
            settle(plugin, instance)
            plugin.set_param(instance, (prefix+'length_ms').encode(), b'250')
            settle(plugin, instance)
            snapshot: str = get(plugin, instance, 'state')
            map_parameter_to_value: dict[str, Any] = json.loads(snapshot)
            assert map_parameter_to_value[prefix+'sample_path'] == str(sample_path)
            assert map_parameter_to_value[prefix+'length_anchor'] == 'ms'
            restored: Any = plugin.create_instance(str(root).encode(), snapshot.encode())
            assert restored
            settle(plugin, restored)
            assert json.loads(get(plugin, restored, 'state')) == map_parameter_to_value
            plugin.set_param(restored, (prefix+'trim_start').encode(), b'0.25')
            settle(plugin, restored)
            assert abs(float(get(plugin, restored, prefix+'length_ms'))-250) < .01
            if engine == 'drums':
                plugin.set_param(instance, b'slice_count', b'2')
                plugin.set_param(instance, b'slice', b'on')
                settle(plugin, instance)
                for pad in (1, 2):
                    assert float(get(plugin, instance, f'p{pad:02d}_length_pct')) == 100
                    assert pathlib.Path(get(plugin, instance, f'p{pad:02d}_sample_path')).is_file()
            plugin.destroy_instance(restored)
            plugin.destroy_instance(instance)
            print(f'{engine}: asynchronous ABI and state round-trip passed')

if __name__ == '__main__':
    main()
