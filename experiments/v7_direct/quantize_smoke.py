"""Structural quantization smoke test. Synthetic calibration is NOT deployment calibration."""
from pathlib import Path
import json,time,argparse
import torch
from torch.utils.data import DataLoader,TensorDataset
from esp_ppq.api import espdl_quantize_onnx
torch.set_num_threads(2)
torch.manual_seed(42)
base=Path(__file__).resolve().parent
assert all(v['passed'] for v in json.loads((base/'all_float_equivalence.json').read_text()).values())
parser=argparse.ArgumentParser();parser.add_argument('--diagnostic',action='store_true');parser.add_argument('--model',choices=['face_landmarks_detector','face_detector','face_blendshapes'],default='face_landmarks_detector');args=parser.parse_args()
stem=args.model
if stem=="face_blendshapes":
 from conservative_layout import install
 install()
shape={'face_landmarks_detector':[1,3,256,256],'face_detector':[1,3,128,128],'face_blendshapes':[1,146,2]}[stem]
x=torch.rand(32,*shape[1:])
if stem=='face_blendshapes': x*=256
if stem=='face_detector': x=x*2-1
loader=DataLoader(TensorDataset(x),batch_size=1,shuffle=False)
start=time.perf_counter()
# Coordinate centering/squaring/norm must not lose small differences in INT8.
# The previous all-INT8 frontend quantized squared values to zero for the
# standard-normal test input, creating a zero denominator before division.
overrides=None
if stem=='face_blendshapes':
 import onnx
 from esp_ppq.core import TargetPlatform
 model=onnx.load(str(base/'models'/f'{stem}.onnx'))
 first_conv=next(i for i,n in enumerate(model.graph.node) if n.op_type=='Conv')
 overrides={n.name:TargetPlatform.FP32 for n in model.graph.node[:first_conv]}
 print('Keeping',len(overrides),'coordinate-normalization operations in FP32')
test_input=torch.randn(shape)
graph=espdl_quantize_onnx(onnx_import_file=str(base/'models'/f'{stem}.onnx'),
 espdl_export_file=str(base/'models'/('face_landmarks_s3_int8_smoke.espdl' if stem=='face_landmarks_detector' else f'{stem}_s3_int8_smoke.espdl')),
 calib_dataloader=loader,calib_steps=32,input_shape=shape,inputs=[test_input],skip_export=args.diagnostic,
 target='esp32s3',quant_type='w8a8',dispatching_override=overrides,device='cpu',collate_fn=lambda batch:batch[0],
 error_report=False,export_test_values=True,verbose=1)
if args.diagnostic:
 assert stem=='face_blendshapes','Diagnostic export is expression-only'
 from esp_ppq.api.espdl_interface import generate_test_value,get_target_platform
 from esp_ppq import lib as PFL
 from esp_ppq.parser.espdl import helper
 from FlatBuffers.Dl.Model import Model,ModelT
 source=base/'models/face_blendshapes_s3_int8_smoke.espdl'
 old=ModelT.InitFromObj(Model.GetRootAs(source.read_bytes(),16)).graph
 available={v.name.decode() for v in old.valueInfo+old.output}
 checkpoints=[]
 for op in graph.topological_sort():
  for var in op.outputs:
   if var.name in available and var.name not in checkpoints:checkpoints.append(var.name)
 values=generate_test_value(graph,'cpu',[test_input],checkpoints)
 out=base/'models/face_blendshapes_s3_int8_diagnostic.espdl'
 PFL.Exporter(platform=get_target_platform('esp32s3',quant_type='w8a8')).export(
  file_path=str(out),graph=graph,values_for_test=values,export_config=True)
 check=ModelT.InitFromObj(Model.GetRootAs(out.read_bytes(),16)).graph
 valid={v.name for v in check.valueInfo+check.output}
 missing=[v.name.decode() for v in check.testOutputsValue if v.name not in valid]
 assert not missing,missing
 print('DIAGNOSTIC export:',len(checkpoints),'intermediate checkpoints, bytes=',out.stat().st_size)
 (base/'diagnostic_checkpoints.json').write_text(json.dumps(checkpoints,indent=2))
print('EXPORTED structural smoke model in' ,time.perf_counter()-start,'seconds')
print('Not deployment-ready: requires real representative face crops and device benchmarking.')
