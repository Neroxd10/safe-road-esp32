"""Recalibrate converted V7 landmarks on augmented public face crops; keep thresholds unchanged."""
from pathlib import Path
import json,torch,numpy as np,onnxruntime as ort
import torch.nn.functional as F
from torch.utils.data import DataLoader,TensorDataset
from esp_ppq.api import espdl_quantize_onnx
from esp_ppq.executor import TorchExecutor
base=Path(__file__).resolve().parent;models=base.parents[1]/'v7_direct/models';torch.set_num_threads(2);torch.manual_seed(617)
source=torch.from_numpy(np.fromfile(base/'landmark_input.bin',np.float32).reshape(1,256,256,3).transpose(0,3,1,2).copy())
def augment(n):
 out=[]
 for i in range(n):
  angle=(torch.rand(1).item()-.5)*.25;scale=.95+torch.rand(1).item()*.1
  c,s=np.cos(angle)*scale,np.sin(angle)*scale
  theta=torch.tensor([[[c,-s,(torch.rand(1).item()-.5)*.07],[s,c,(torch.rand(1).item()-.5)*.07]]],dtype=torch.float32)
  grid=F.affine_grid(theta,source.shape,align_corners=False)
  image=F.grid_sample(source,grid,mode='bilinear',padding_mode='border',align_corners=False)
  brightness=.75+torch.rand(1).item()*.5;contrast=.8+torch.rand(1).item()*.4
  image=((image-.5)*contrast+.5)*brightness
  out.append(torch.clamp(image+torch.randn_like(image)*.003,0,1))
 return torch.cat(out)
calib=augment(32);torch.manual_seed(1081);test=torch.cat([source,augment(8)])
session=ort.InferenceSession(str(models/'face_landmarks_detector.onnx'),providers=['CPUExecutionProvider'])
report={}
for quant in ['w8a8','w16a16']:
 graph=espdl_quantize_onnx(str(models/'face_landmarks_detector.onnx'),str(base/f'landmark_faces_{quant}.espdl'),DataLoader(TensorDataset(calib),batch_size=1,shuffle=False),32,[1,3,256,256],inputs=[source],target='esp32s3',quant_type=quant,device='cpu',collate_fn=lambda batch:batch[0],error_report=False,export_test_values=True,export_config=False,verbose=0)
 executor=TorchExecutor(graph=graph,device='cpu');rows=[]
 for image in test:
  x=image.unsqueeze(0);got=executor.forward(x);names=list(graph.outputs);pred={name:got[i].detach().numpy() for i,name in enumerate(names)}
  expected=session.run(None,{session.get_inputs()[0].name:x.numpy()});reference={name.name:expected[i] for i,name in enumerate(session.get_outputs())}
  confidence=float(torch.sigmoid(torch.from_numpy(pred['Identity_1'])).item());coords=pred['Identity'].reshape(478,3);refcoords=reference['Identity'].reshape(478,3)
  rows.append({'presence':confidence,'reference_presence':float(1/(1+np.exp(-reference['Identity_1'].item()))),'mean_xy_error_crop_pixels':float(np.abs(coords[:,:2]-refcoords[:,:2]).mean()),'max_xy_error_crop_pixels':float(np.abs(coords[:,:2]-refcoords[:,:2]).max())})
 report[quant]={'cases':rows,'artifact':str(base/f'landmark_faces_{quant}.espdl'),'bytes':(base/f'landmark_faces_{quant}.espdl').stat().st_size}
 print('FACE CALIBRATION RESULT',quant,json.dumps(rows),flush=True)
 if all(r['presence']>.6 and r['mean_xy_error_crop_pixels']<5 and r['max_xy_error_crop_pixels']<20 for r in rows):
  report['selected']=quant;break
(base/'face_calibration_report.json').write_text(json.dumps(report,indent=2))
