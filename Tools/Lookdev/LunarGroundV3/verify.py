"""Byte/normal/actual HLSL checks. No GPU or Unreal rendering claim."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import sys
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT/"Content/Star/Art/LunarGroundV3"
sys.path.insert(0, str(ROOT/"Tools/Unreal/Materials"))
from verify_materials import _blob_bytes, _release


def compile_shader(filename):
    normal = filename == "SurfaceNormal.ush"
    textures = ("DayTex", "RegionalTex", "DetailTex", "MesoTex")
    scalars = ("AlbedoScale", "RegionalStrength", "DetailScale", "DetailStrength", "SurfaceRoughness", "NormalStrength", "MesoScale", "MesoStrength")
    declarations = "\n".join(f"Texture2D {n}; SamplerState {n}Sampler;" for n in textures)
    declarations += "\n"+"\n".join(f"float {n};" for n in scalars)
    source = "#define Texture2DSample(T,S,U) T.Sample(S,U)\n"+declarations
    source += "\nfloat3 DetailNormal; float3 MesoNormal;\n"
    source += ("float3" if normal else "float4")+" shade(float2 UV,float2 DetailUV) {\n"
    source += (ROOT/"Shaders/Star"/filename).read_text(encoding="utf-8")+"\n}\n"
    source += "float4 main(float2 uv:TEXCOORD0):SV_Target { return "+("float4(shade(uv,uv),1);" if normal else "shade(uv,uv);")+" }"
    data=source.encode("utf-8")
    compile_=ctypes.WinDLL("d3dcompiler_47.dll").D3DCompile
    compile_.restype=ctypes.c_long
    compile_.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_void_p)]
    code,error=ctypes.c_void_p(),ctypes.c_void_p()
    status=compile_(data,len(data),filename.encode(),None,None,b"main",b"ps_5_0",1<<11,0,ctypes.byref(code),ctypes.byref(error))
    try:
        messages=_blob_bytes(error).decode("utf-8",errors="replace")
        if status<0: raise AssertionError(messages)
        return {"shader":filename,"compiledBytes":len(_blob_bytes(code)),"messages":messages}
    finally:
        _release(code); _release(error)


def verify(source):
    meta=json.loads((OUT/"provenance.json").read_text(encoding="utf-8"))
    for name,expected in meta["artifacts"].items():
        assert hashlib.sha256((OUT/name).read_bytes()).hexdigest()==expected, name
    assert hashlib.sha256(source.read_bytes()).hexdigest()==meta["source"]["fileSha256"]
    crop=np.asarray(Image.open(source).convert("RGB").crop(meta["source"]["cropPixels"]))
    assert np.array_equal(crop,np.asarray(Image.open(OUT/"AS17-147-22501_observed_crop.png"))), "original photograph pixels changed"
    base=np.asarray(Image.open(OUT/"apollo17_meso_linear.png"),dtype=float)/255
    normal=np.asarray(Image.open(OUT/"apollo17_meso_normal_dx.png"),dtype=float)/255*2-1
    assert base.shape==(1024,1024,3) and normal.shape==base.shape
    assert np.max(np.abs(np.linalg.norm(normal,axis=-1)-1))<.008, "normal encoding"
    slopes=np.linalg.norm(normal[:,:,:2],axis=-1)/normal[:,:,2]
    assert float(slopes.max())<.23, "estimated slope bound"
    assert np.max(np.abs(base[[0,-1],:,0]-.5))<.003 and np.max(np.abs(base[:,[0,-1],0]-.5))<.003, "wrap boundary neutral"
    assert .02<float(base[:,:,0].std())<.08, "useful bounded photo contrast"
    assert meta["tileMeters"]==2.0
    compiled=[compile_shader(n) for n in ("Surface.ush","SurfaceNormal.ush")]
    return {"status":"SOURCE_CHECKS_PASS", "sourceCropPixelsPreserved":True, "normalSlopeMax":float(slopes.max()), "hlsl":compiled, "unrealGpuValidated":False}


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("source",type=Path)
    print(json.dumps(verify(parser.parse_args().source),indent=2))
