from pathlib import Path
import numpy as np,math
from esp_ppq.parser.espdl import helper
from FlatBuffers.Dl.Model import Model,ModelT
base=Path('experiments/v7_direct');g=ModelT.InitFromObj(Model.GetRootAs((base/'models/face_blendshapes_s3_int8_diagnostic.espdl').read_bytes(),16)).graph
D={1:'<f4',3:'i1',5:'<i2',6:'<i4',7:'<i8'}
def array(t):
 raw=b''.join(v.bytes.tobytes() for v in t.rawData);return np.frombuffer(raw,dtype=D[t.dataType])[:math.prod(t.dims)].reshape(t.dims)
vals={t.name:array(t) for t in g.initializer+g.testInputsValue};exps={t.name:int(t.exponents[0]) if t.exponents is not None and len(t.exponents) else 0 for t in g.initializer+g.testInputsValue}
types={v.name:v.valueInfoType.value.elemType for v in g.valueInfo+g.input+g.output};exps.update({v.name:int(v.exponents[0]) if v.exponents is not None and len(v.exponents) else 0 for v in g.valueInfo+g.input+g.output});expected={t.name:array(t) for t in g.testOutputsValue}
def attrs(n):
 return {a.name.decode():(a.i.i if a.i else list(map(int,a.ints)) if a.ints is not None else None) for a in n.attribute}
checks=[]
for n in g.node:
 op=n.opType.decode();a=attrs(n);z=n.output[0];xs=[vals[i] for i in n.input];real=[v.astype(float)*2.**exps[k] if np.issubdtype(v.dtype,np.integer) else v for k,v in zip(n.input,xs)]
 if op=='Conv':print('Reached first convolution; review per-checkpoint errors above.');break
 if op.startswith('Reduce'):
  axes=tuple(map(int,xs[1].flat));r=(np.mean if op=='ReduceMean' else np.sum)(real[0],axis=axes,keepdims=bool(a.get('keepdims',1)))
 elif op in ('Add','Sub','Mul','Div'):
  with np.errstate(all='ignore'):r={'Add':np.add,'Sub':np.subtract,'Mul':np.multiply,'Div':np.divide}[op](*real[:2])
 elif op in ('QuantizeLinear','DequantizeLinear','RequantizeLinear'):r=real[0]
 elif op=='Sqrt':r=np.sqrt(real[0])
 elif op=='Neg':r=-real[0]
 elif op=='Transpose':r=real[0].transpose(a['perm'])
 elif op=='Reshape':r=real[0].reshape(list(map(int,xs[1].flat)))
 elif op=='Slice':
  slices=[slice(None)]*real[0].ndim
  for j,(start,end) in enumerate(zip(xs[1].flat,xs[2].flat)):
   axis=int(xs[3].flat[j]) if len(xs)>3 else j;step=int(xs[4].flat[j]) if len(xs)>4 else 1;slices[axis]=slice(int(start),int(end),step)
  r=real[0][tuple(slices)]
 else:print('Unhandled',op);break
 if not np.all(np.isfinite(r)):raise RuntimeError('Non-finite intermediate: '+z.decode())
 if types[z] in (3,5):
  bits=8 if types[z]==3 else 16;r=np.clip(np.floor(r/(2.**exps[z])+0.5),-(2**(bits-1)),2**(bits-1)-1).astype(D[types[z]])
 else:r=np.asarray(r,np.float32)
 vals[z]=r
 if z in expected:
  err=np.nanmax(abs(r.astype(float)-expected[z]));checks.append({'op':op,'name':z.decode(),'max_abs':float(err)});print(op,'err',float(err),'range',float(np.nanmin(r)),float(np.nanmax(r)))

report={"passed":all(c["max_abs"]<0.0005 for c in checks),"checks":checks,"scope":"coordinate frontend before first Conv; PC only"}
(base/"expression_front_check.json").write_text(__import__("json").dumps(report,indent=2))
if not report["passed"]:raise SystemExit("Frontend arithmetic check failed")
