import logging,traceback
logging.disable(logging.CRITICAL)
from tflite2onnx.tensor import Tensor
orig=Tensor.transform
def transform(self):
 if self.layout and len(self.shape)==3 and len(self.layout.source)==4 and self.shape[:2]==[1,1]:
  self.shape.insert(0,1)
 try: return orig(self)
 except Exception:
  print('FAILED TENSOR',self.name,self.shape,self.layout);raise
Tensor.transform=transform
import tflite2onnx,onnx
p='experiments/v7_direct/models/face_landmarks_detector'
tflite2onnx.convert(p+'.tflite',p+'.onnx')
# ESP-DL PRelu requires slopes [C,1,1], rather than broadcast [1,C,1,1].
# This is equivalent in NCHW ONNX and avoids a runtime shape assertion.
from onnx import numpy_helper
model=onnx.load(p+'.onnx')
slopes={node.input[1] for node in model.graph.node if node.op_type=='PRelu'}
for init in model.graph.initializer:
 if init.name in slopes:
  values=numpy_helper.to_array(init)
  assert values.ndim==4 and values.shape[0]==1 and values.shape[2:]==(1,1)
  init.CopyFrom(numpy_helper.from_array(values.reshape(values.shape[1],1,1),init.name))
for value in model.graph.value_info:
 if value.name in slopes:
  del value.type.tensor_type.shape.dim[0]
onnx.checker.check_model(model)
onnx.save(model,p+'.onnx')
print('LANDMARK CONVERSION PASSED; per-channel PRelu slopes canonicalized')
