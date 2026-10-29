"""Check bounded public OptiScaler probes without loading any DLL or executing GPU work."""
import array
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

root, baseline, new_report = map(Path, sys.argv[1:])
assert not new_report.exists(), "Refusing prior evidence"
sha = lambda b: hashlib.sha256(b).hexdigest()
provenance = json.loads((root / "package-provenance.json").read_text())
assert provenance["declaredCommit"] == "6125a75b37f7e2a408e31934f4827e5660429203"
assert sha((root / "OptiScaler-NR-v0.8.10.zip").read_bytes()) == provenance["sha256"]
pe = json.loads((root / "pe-interface.json").read_text())
assert sha((root / "package/OptiScaler.dll").read_bytes()) == pe["sha256"]
required = ["Init_with_ProjectID", "GetCapabilityParameters", "CreateFeature", "EvaluateFeature", "ReleaseFeature", "DestroyParameters", "Shutdown1", "Shutdown"]
assert all("NVSDK_NGX_D3D11_" + name in pe["ngxExports"] for name in required)
expected = baseline.read_bytes()
assert struct.unpack_from("<4I", expected) == (2560, 1440, 10, 8)
assert len(expected) == 16 + 2560 * 1440 * 8
words = array.array("H", expected[16:])
assert sys.byteorder == "little"
assert all((word & 0x7c00) != 0x7c00 for word in words)
max_word = max(word for i, word in enumerate(words) if i % 4 != 3 and word < 0x8000)
maximum = struct.unpack("<e", struct.pack("<H", max_word))[0]
assert maximum > 1
cases = [
    ("probe-v1", "host-v1", 1, 112),
    ("probe-fl111", "host-fl111", 5, 0),
    ("probe-shutdown-base", "host-shutdown-base", 0, 0),
    ("probe-device-shutdown-trace", "host-device-shutdown-trace", 5, 0),
]
results = []
for name, host, status, debug_errors in cases:
    exit_info = json.loads((root / (name + "-exit.json")).read_text())
    log = (root / (name + "-process.log")).read_text()
    backend = (root / host / "OptiScaler.log").read_text(encoding="utf-8-sig")
    assert exit_info["status"] == status and not exit_info["error"]
    assert "EvaluateFeature result=0x1" in log
    assert len(re.findall(r"^EvaluateAttempt ", log, re.M)) == 1
    assert len(re.findall(r"^InputExact index=", log, re.M)) == 4
    assert "OutputFiniteRGB=11059200 max=12408 nonidentity=1" in log
    assert "D3D11DebugErrors=" + str(debug_errors) in log
    assert "Upscalers.Dx11Upscaler: dlss_12" in backend
    assert "Creating new DLSS w/Dx12 feature" in backend
    assert "DLSS v310.9.16 loaded." in backend
    assert "NgxDltss::EvaluateDltss" in backend
    assert "upscale ok: true, enabled: false" in backend
    output = (root / name / "sr-output.raw").read_bytes()
    assert output == expected, "Output differs from direct NGX baseline"
    assert sha((root / host / "OptiScaler.dll").read_bytes()) == pe["sha256"]
    if status == 0:
        for stage in ["ReleaseFeature", "DestroyParameters", "Shutdown"]:
            assert stage + " result=0x1" in log
        assert "FeatureLevel=0xb100" in log
        assert "ShutdownEnter variant=base" in log
        assert (root / name / "probe-complete.txt").exists()
        assert not (root / name / "INCOMPLETE").exists()
    else:
        assert (root / name / "INCOMPLETE").exists()
        assert not (root / name / "probe-complete.txt").exists()
    if name == "probe-device-shutdown-trace":
        assert "ShutdownEnter variant=device" in log
        assert "NativeException code=0xc0000005" in log and "_nvngx.dll offset=0x44e54" in log
    results.append({"run": name, "exit": status, "d3d11DebugErrorsBeforeCleanup": debug_errors,
                    "wholeRawEqualsDirectNgx": True, "sha256": sha(output), "fullProbePassed": status == 0})
native_name = "probe-native12"
native_log = (root / (native_name + "-process.log")).read_text()
native_exit = json.loads((root / (native_name + "-exit.json")).read_text())
native_backend = (root / "host-native12/OptiScaler.log").read_text(encoding="utf-8-sig")
assert native_exit["status"] == 0 and not native_exit["error"]
assert "FeatureLevel=0xb000" in native_log
for stage in ["Init12", "CreateFeature12", "EvaluateFeature12", "ReleaseFeature12", "DestroyParameters12", "Shutdown12"]:
    assert stage + " result=0x1" in native_log
assert len(re.findall(r"^Evaluate12Attempt ", native_log, re.M)) == 1
assert len(re.findall(r"^InputExact index=", native_log, re.M)) == 4
assert "BothDebugErrors=0" in native_log and "OutputFiniteRGB=11059200 max=12408" in native_log
assert "Upscalers.Dx12Upscaler: dlss" in native_backend
assert "DLSS v310.9.16 loaded." in native_backend
assert "NVSDK_NGX_D3D12_EvaluateFeature EvaluateFeature" in native_backend
assert "IFeature_Dx11wDx12" not in native_backend
assert (root / native_name / "native-complete.txt").exists() and not (root / native_name / "INCOMPLETE").exists()
native_output = (root / native_name / "sr-output.raw").read_bytes()
assert native_output == expected
assert sha((root / "host-native12/OptiScaler.dll").read_bytes()) == pe["sha256"]
results.append({"run": native_name, "entry": "public D3D12 via existing shared bridge", "exit": 0,
                "bothApiDebugErrorsAfterCleanup": 0, "wholeRawEqualsDirectNgx": True,
                "sha256": sha(native_output), "fullProbePassed": True})
report = {"kind": "OPTISCALER_PUBLIC_SR_INTERFACE", "release": provenance,
          "dllSha256": pe["sha256"], "cases": results, "finiteRgbaComponents": len(words),
          "maximumRgb": maximum, "inputReadbacksExact": 20, "evaluations": {"sr": 5, "nr": 0},
          "qualified": {"featureLevel": "11.1", "backend": "dlss_12", "shutdown": "base public export",
                        "sameSizeHdrDlaa": True},
          "qualifiedNative": {"entry": "D3D12", "inputFeatureLevel": "11.0", "sharedBridge": "project D3D11-origin NT-handle bridge", "shutdown": "public D3D12_Shutdown"},
          "optiD3d11FL11_0Qualified": False, "deviceShutdownQualified": False, "hotUnloadQualified": False,
          "preSrNrExecuted": False, "fgExecuted": False, "performanceAccepted": False,
          "limits": "Fresh isolated processes, one Reset evaluation each, P1 uncalibrated and captured jitter0. "
                    "Package DllMain installs process-global hooks; this is not a standalone SDK contract. "
                    "No FreeLibrary, repeated Init/Shutdown, game integration, upscaling, full state-preservation or timing acceptance. "
                    "Output equivalence certifies the tested SR path only; NR HDR proxy/recovery policies remain to qualify."}
new_report.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"cases": results, "evaluations": report["evaluations"], "qualified": report["qualified"],
                  "qualifiedNative": report["qualifiedNative"], "preSrNrExecuted": False, "performanceAccepted": False}, indent=2))
