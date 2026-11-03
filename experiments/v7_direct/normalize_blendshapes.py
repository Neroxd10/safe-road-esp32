from pathlib import Path
import onnx,numpy as np
from onnx import helper,numpy_helper
p=Path('experiments/v7_direct/models/face_blendshapes.onnx');m=onnx.load(p)
count=0
for node in m.graph.node:
 if node.op_type=='Reciprocal':
  one='v7_one_'+str(count);m.graph.initializer.append(numpy_helper.from_array(np.ones((1,),np.float32),one))
  old=node.input[0];node.op_type='Div';del node.input[:];node.input.extend([one,old]);count+=1
onnx.checker.check_model(m);onnx.save(m,p);print('Replaced',count,'unsupported Reciprocal nodes with Div(1,x)')
