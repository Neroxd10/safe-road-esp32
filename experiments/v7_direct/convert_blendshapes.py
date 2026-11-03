import logging
logging.disable(logging.CRITICAL)
import numpy as np,tflite,tflite2onnx,onnx
# Generated scalar shapes can be integer zero; normalize the empty shape.
original=tflite.Tensor.ShapeAsNumpy
def shape(self):
 value=original(self)
 return np.array([],dtype=np.int32) if isinstance(value,int) else value
tflite.Tensor.ShapeAsNumpy=shape
from tflite2onnx.tensor import Tensor
add_consumer=Tensor.addConsumer
def consumer(self,op):
 if op not in self.consumers: add_consumer(self,op)
Tensor.addConsumer=consumer
from tflite2onnx.op.reduce import Reduce
from tflite2onnx.op.unary import Unary
from tflite2onnx.op.common import OpFactory
Reduce.TypeMapping[tflite.BuiltinOperator.SUM]='ReduceSum'
OpFactory.registry[tflite.BuiltinOperator.SUM]=Reduce
Unary.TypeMapping[tflite.BuiltinOperator.NEG]='Neg'
OpFactory.registry[tflite.BuiltinOperator.NEG]=Unary
from tflite2onnx.op.binary import Binary
Binary.TypeMapping[tflite.BuiltinOperator.DIV]='Div'
OpFactory.registry[tflite.BuiltinOperator.DIV]=Binary
p='experiments/v7_direct/models/face_blendshapes'
tflite2onnx.convert(p+'.tflite',p+'.onnx');onnx.checker.check_model(p+'.onnx');print('BLENDSHAPE CONVERSION PASSED')
