import os
os.environ['TF_CPP_MIN_LOG_LEVEL']='2';os.environ['TF_ENABLE_ONEDNN_OPTS']='0'
from pathlib import Path
import json,numpy as np,onnx,onnxruntime as ort,tensorflow as tf
base=Path(__file__).resolve().parent
reports={}
for stem in ('face_detector','face_landmarks_detector','face_blendshapes'):
 onnx.checker.check_model(str(base/'models'/f'{stem}.onnx'))
 lite=tf.lite.Interpreter(model_path=str(base/'models'/f'{stem}.tflite'),num_threads=2);lite.allocate_tensors()
 session=ort.InferenceSession(str(base/'models'/f'{stem}.onnx'),providers=['CPUExecutionProvider'])
 shape=tuple(lite.get_input_details()[0]['shape']);rng=np.random.default_rng(100);errors=[]
 for i in range(5):
  x=rng.random(shape,dtype=np.float32)
  if stem=='face_blendshapes':x*=256
  lite.set_tensor(lite.get_input_details()[0]['index'],x);lite.invoke()
  expected=[lite.get_tensor(o['index']) for o in lite.get_output_details()]
  input=x.transpose(0,3,1,2).copy() if x.ndim==4 else x
  actual=session.run(None,{session.get_inputs()[0].name:input})
  case=[]
  for ref,got in zip(expected,actual):
   if ref.ndim==4:got=got.transpose(0,2,3,1)
   assert ref.shape==got.shape,(stem,ref.shape,got.shape)
   case.append(float(np.abs(ref-got).max()))
  errors.append(case)
 reports[stem]={'max_abs':max(max(e) for e in errors),'passed':max(max(e) for e in errors)<0.01,'per_case_output_max_abs':errors}
print(json.dumps(reports,indent=2));(base/'all_float_equivalence.json').write_text(json.dumps(reports,indent=2))
if not all(v['passed'] for v in reports.values()):raise SystemExit(1)
