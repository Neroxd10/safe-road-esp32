from pathlib import Path
import torch,numpy as np,json
from torch.utils.data import DataLoader,TensorDataset
from esp_ppq.api import espdl_quantize_onnx
from esp_ppq.executor import TorchExecutor
base=Path(__file__).resolve().parent;models=base.parents[1]/'v7_direct/models'
torch.set_num_threads(2);results={}
for name,shape,filename in [('face_detector',[1,3,128,128],'detector_input.bin'),('face_landmarks_detector',[1,3,256,256],'landmark_input.bin')]:
 torch.manual_seed(42);x=torch.rand(32,*shape[1:]);
 if name=='face_detector':x=x*2-1
 graph=espdl_quantize_onnx(str(models/f'{name}.onnx'),str(base/'unused.espdl'),DataLoader(TensorDataset(x),batch_size=1),32,shape,target='esp32s3',quant_type='w8a8',collate_fn=lambda batch:batch[0],device='cpu',error_report=False,skip_export=True,verbose=0)
 real=np.fromfile(base/filename,np.float32).reshape(1,shape[2],shape[3],3).transpose(0,3,1,2).copy()
 outputs=TorchExecutor(graph=graph,device='cpu').forward(torch.from_numpy(real))
 if name=='face_detector':
  logits=next(o for o in outputs if o.numel()==896);score=float(torch.sigmoid(logits).max());results[name]={'best_score':score}
 else:
  out={key:outputs[i] for i,key in enumerate(graph.outputs)};results[name]={'landmark_presence':float(torch.sigmoid(out['Identity_1']).item())}
 print('REAL IMAGE CHECK',name,results[name],flush=True)
(base/'quantized_portrait_report.json').write_text(json.dumps(results,indent=2))
