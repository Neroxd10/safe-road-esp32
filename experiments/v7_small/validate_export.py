from pathlib import Path
import json,hashlib,torch,numpy as np,onnx,onnxruntime as ort,tensorflow as tf
import torch.nn.functional as F
from torch.utils.data import DataLoader,TensorDataset
from esp_ppq.api import espdl_quantize_onnx
from esp_ppq.executor import TorchExecutor
base=Path(__file__).resolve().parent
models=base/'models';torch.set_num_threads(2);torch.manual_seed(619)
source=torch.from_numpy(np.fromfile(base.parent/'v7_live/tests/landmark_input.bin',np.float32).reshape(1,256,256,3).transpose(0,3,1,2).copy())
source=F.interpolate(source,size=(192,192),mode='bilinear',align_corners=False)
np.save(base/'tests/reference_crop.npy',source.numpy())
def augment(n):
 out=[]
 for i in range(n):
  angle=(torch.rand(1).item()-.5)*.3;scale=.92+torch.rand(1).item()*.16
  c,s=np.cos(angle)*scale,np.sin(angle)*scale
  theta=torch.tensor([[[c,-s,(torch.rand(1).item()-.5)*.1],[s,c,(torch.rand(1).item()-.5)*.1]]],dtype=torch.float32)
  grid=F.affine_grid(theta,source.shape,align_corners=False)
  image=F.grid_sample(source,grid,mode='bilinear',padding_mode='border',align_corners=False)
  image=((image-.5)*(.8+torch.rand(1).item()*.4)+.5)*(.75+torch.rand(1).item()*.5)
  out.append(torch.clamp(image+torch.randn_like(image)*.003,0,1))
 return torch.cat(out)
calib=augment(32);torch.manual_seed(1082);test=torch.cat([source,augment(16),torch.zeros_like(source),torch.ones_like(source)])
session=ort.InferenceSession(str(models/'face_landmark.onnx'),providers=['CPUExecutionProvider'])
interpreter=tf.lite.Interpreter(model_path=str(models/'face_landmark.tflite'),num_threads=2);interpreter.allocate_tensors()
float_rows=[];references=[]
for x in test:
 image=x.unsqueeze(0).numpy();expected=session.run(None,{session.get_inputs()[0].name:image})
 interpreter.set_tensor(interpreter.get_input_details()[0]['index'],image.transpose(0,2,3,1).copy());interpreter.invoke()
 tfout=[interpreter.get_tensor(d['index']) for d in interpreter.get_output_details()]
 errors=[]
 for value in expected:
  original=next(t for t in tfout if t.size==value.size)
  errors.append(float(np.abs(original.reshape(-1)-value.reshape(-1)).max()))
 assert max(errors)<.01,errors
 float_rows.append({'max_conversion_error':max(errors)});references.append(expected)
# Count Conv/FC MACs using inferred tensor dimensions, including depthwise groups.
def macs(path):
 model=onnx.shape_inference.infer_shapes(onnx.load(path));shapes={v.name:[d.dim_value for d in v.type.tensor_type.shape.dim] for v in list(model.graph.value_info)+list(model.graph.input)+list(model.graph.output)}
 weights={v.name:list(v.dims) for v in model.graph.initializer};total=0
 for node in model.graph.node:
  if node.op_type=='Conv':
   shape=shapes[node.output[0]];weight=weights[node.input[1]]
   total+=int(np.prod(shape))*int(np.prod(weight[1:]))
 return total
report={'source':'https://storage.googleapis.com/mediapipe-assets/face_landmark.tflite','float_conversion':float_rows,'validation_scope':'19 transformed inputs of one public portrait plus constant images; no real closed-eye/yawn/pose accuracy claim','old_conv_macs':macs(base.parent/'v7_direct/models/face_landmarks_detector.onnx'),'small_conv_macs':macs(models/'face_landmark.onnx'),'quantization':{}}
def metrics(x):
 p=x.reshape(468,3)[:,:2]
 def dist(a,b):return float(np.linalg.norm(p[a]-p[b]))
 def ear(ids):return (dist(ids[1],ids[5])+dist(ids[2],ids[4]))/max(1e-5,2*dist(ids[0],ids[3]))
 return np.array([ear([33,160,158,133,153,144]),ear([362,385,387,263,373,380]),dist(13,14)/max(1e-5,dist(78,308))])
for quant in ['w8a8','w16a16']:
 artifact=models/f'landmark_{quant}.espdl'
 graph=espdl_quantize_onnx(str(models/'face_landmark.onnx'),str(artifact),DataLoader(TensorDataset(calib),batch_size=1),32,[1,3,192,192],inputs=[source],target='esp32s3',quant_type=quant,device='cpu',collate_fn=lambda batch:batch[0],error_report=False,export_test_values=True,export_config=False,verbose=0)
 executor=TorchExecutor(graph=graph,device='cpu');rows=[]
 for i,image in enumerate(test):
  got=executor.forward(image.unsqueeze(0));pred={name:got[k].detach().numpy() for k,name in enumerate(graph.outputs)}
  coords=pred['conv2d_21'].reshape(468,3);ref=references[i][0].reshape(468,3)
  logit=float(pred['conv2d_31'].reshape(-1)[0]);reference_logit=float(references[i][1].reshape(-1)[0])
  presence=float(1/(1+np.exp(-np.clip(logit,-80,80))));reference_presence=float(1/(1+np.exp(-np.clip(reference_logit,-80,80))))
  row={'presence':presence,'reference_presence':reference_presence,'logit_error':abs(logit-reference_logit),'mean_xy_px':float(np.abs(coords[:,:2]-ref[:,:2]).mean()),'max_xy_px':float(np.abs(coords[:,:2]-ref[:,:2]).max()),'geometry_error':np.abs(metrics(coords)-metrics(ref)).tolist()}
  row['passed']=row['mean_xy_px']<.5 and row['max_xy_px']<2 and max(row['geometry_error'])<.025 and ((presence>.6)==(reference_presence>.6))
  rows.append(row)
 passed=all(r['passed'] for r in rows)
 report['quantization'][quant]={'cases':rows,'passed':passed,'bytes':artifact.stat().st_size,'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest()}
 print('CANDIDATE',quant,'passed',passed,'max_mean_xy',max(r['mean_xy_px'] for r in rows),'max_xy',max(r['max_xy_px'] for r in rows),flush=True)
 (base/'validation.json').write_text(json.dumps(report,indent=2))
 if passed:
  report['selected']=quant
  import shutil;shutil.copy2(artifact,base/'main/landmark.espdl')
  break
(base/'validation.json').write_text(json.dumps(report,indent=2))
assert 'selected' in report,'No candidate met quantization equivalence gates; do not flash.'
print('SELECTED',report['selected'],'Conv MAC ratio',report['small_conv_macs']/report['old_conv_macs'],flush=True)
