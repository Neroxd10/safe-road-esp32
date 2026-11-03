import os
os.environ['TF_CPP_MIN_LOG_LEVEL']='2'
os.environ['TF_ENABLE_ONEDNN_OPTS']='0'
from pathlib import Path
import json,time
import numpy as np
import onnxruntime as ort
import tensorflow as tf
base=Path(__file__).resolve().parent
stem='face_landmarks_detector'
lite=tf.lite.Interpreter(model_path=str(base/'models'/f'{stem}.tflite'),num_threads=2)
lite.allocate_tensors()
session=ort.InferenceSession(str(base/'models'/f'{stem}.onnx'),providers=['CPUExecutionProvider'])
rng=np.random.default_rng(42)
results=[]
for case in range(5):
 x=rng.random((1,256,256,3),dtype=np.float32) if case>1 else np.full((1,256,256,3),case*0.5,np.float32)
 lite.set_tensor(lite.get_input_details()[0]['index'],x);lite.invoke()
 expected=[lite.get_tensor(o['index']) for o in lite.get_output_details()]
 got=session.run(None,{session.get_inputs()[0].name:x.transpose(0,3,1,2).copy()})
 errors=[]
 for ref,actual in zip(expected,got):
  if ref.ndim==4:actual=actual.transpose(0,2,3,1)
  assert actual.shape==ref.shape,(actual.shape,ref.shape)
  errors.append({'max_abs':float(np.abs(ref-actual).max()),'mean_abs':float(np.abs(ref-actual).mean())})
 results.append(errors)
passed=all(e['max_abs']<0.01 for r in results for e in r)
report={'passed':passed,'cases':'five synthetic tensors, not detection accuracy tests','tolerance_max_abs':0.01,'output_errors':results}
(base/'float_equivalence.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
if not passed:raise SystemExit('Conversion is NOT numerically equivalent; do not quantize.')
