"""Verify pages through the DSP ABI used by Schwung, not only the manifest."""
import json
import test_plugin_abi as abi
from typing import Any

for module_id in ("warpmrsample", "warpmelodic", "warpmrdrums", "warpdrumkit"):
    engine: str = "drums" if module_id in {"warpmrdrums", "warpdrumkit"} else "melodic"
    library: Any
    api: Any
    library, api = abi.api(abi.R / f"build/host/{engine}_dsp.so")
    instance: Any = api.create_instance(str(abi.R / "modules" / module_id).encode(), b"{}")
    try:
        buffer: Any = abi.ctypes.create_string_buffer(32768)
        length: int = api.get_param(instance, b"ui_hierarchy", buffer, len(buffer))
        assert 0 < length < len(buffer)
        hierarchy: dict[str, Any] = json.loads(buffer.value)
        assert "root" in hierarchy["levels"]
        assert api.get_param(instance, b"chain_params", buffer, len(buffer)) > 0
        parameters: list[dict[str, Any]] = json.loads(buffer.value)
        parameter: dict[str, Any]
        for parameter in parameters:
            if parameter.get("access") != "write":
                abi.get(api, instance, parameter["key"])
        print(f"{module_id}: DSP pages and readable controls passed")
    finally:
        api.destroy_instance(instance)
