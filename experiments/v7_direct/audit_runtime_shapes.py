"""Audit exported blendshape shapes against ESP-DL module shape rules."""
from pathlib import Path
import json,math,argparse,numpy as np
from esp_ppq.parser.espdl import helper
from FlatBuffers.Dl.Model import Model,ModelT
base=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--model',type=Path,default=base/'models/face_blendshapes_s3_int8_smoke.espdl');parser.add_argument('--out',type=Path,default=base/'shape_audit.json');args=parser.parse_args()
g=ModelT.InitFromObj(Model.GetRootAs(args.model.read_bytes(),16)).graph
expected={v.name:[int(d.value.dimValue) for d in v.valueInfoType.value.shape.dim] for v in g.valueInfo+g.input+g.output}
shapes={v.name:expected[v.name] for v in g.input};const={}
for t in g.initializer:
 shapes[t.name]=list(map(int,t.dims))
 if t.dataType in (6,7):
  raw=b''.join(b.bytes.tobytes() for b in t.rawData)
  const[t.name]=np.frombuffer(raw,dtype='<i8' if t.dataType==7 else '<i4')[:math.prod(t.dims)].astype(np.int64).tolist()
def attr(n,key,default=None):
 a=next((a for a in n.attribute if a.name.decode()==key),None)
 if a is None:return default
 if a.i is not None:return a.i.i
 if a.ints is not None:return [int(x) for x in a.ints]
 return default
mismatches=[]
completed=False
for i,n in enumerate(g.node):
 op=n.opType.decode();ins=[shapes[x] for x in n.input];x=ins[0].copy()
 if op in ('Add','Sub','Mul','Div'):
  out=list(np.broadcast_shapes(*ins[:2]))
 elif op.startswith('Reduce'):
  axes=const.get(n.input[1],[]) if len(n.input)>1 else []
  axes=[a%len(x) for a in axes] if axes else ([] if attr(n,'noop_with_empty_axes',0) else list(range(len(x))))
  out=[1 if j in axes else d for j,d in enumerate(x)] if attr(n,'keepdims',1) else [d for j,d in enumerate(x) if j not in axes] or [1]
 elif op=='Reshape':
  target=const[n.input[1]].copy();out=[x[j] if d==0 else d for j,d in enumerate(target)]
  if -1 in out:out[out.index(-1)]=math.prod(x)//(-math.prod(out))
  if math.prod(out)!=math.prod(x):
   print('RESHAPE ASSERT',i,n.name.decode(),'actual input',x,'expected input',expected.get(n.input[0]),'target',target);break
 elif op=='Transpose':out=[x[j] for j in attr(n,'perm',list(reversed(range(len(x)))))]
 elif op=='Concat':
  axis=attr(n,'axis',0)%len(x);out=x;out[axis]=sum(s[axis] for s in ins)
 elif op=='Slice':
  out=x.copy();starts=const[n.input[1]];ends=const[n.input[2]];axes=const[n.input[3]] if len(n.input)>3 else list(range(len(starts)));steps=const[n.input[4]] if len(n.input)>4 else [1]*len(starts)
  for a,b,axis,step in zip(starts,ends,axes,steps):out[axis]=len(range(*slice(a,b,step).indices(x[axis])))
 elif op=='Conv':
  w=ins[1];pads=attr(n,'pads',[0]*4);dil=attr(n,'dilations',[1,1]);stride=attr(n,'strides',[1,1]);out=x.copy();out[1]=(x[1]+pads[0]+pads[1]-dil[0]*(w[0]-1)-1)//stride[0]+1
  if len(x)==4:out[2]=(x[2]+pads[2]+pads[3]-dil[1]*(w[1]-1)-1)//stride[1]+1;out[3]=w[3] if attr(n,'group',1)==1 else x[3]
  else:out[2]=w[2] if attr(n,'group',1)==1 else x[2]
 elif op=='Squeeze':
  axes=const[n.input[1]] if len(n.input)>1 else list(range(len(x)));axes=[a%len(x) for a in axes];out=[d for j,d in enumerate(x) if not(d==1 and j in axes)]
 elif op in ('Sqrt','Neg','Sigmoid','QuantizeLinear','DequantizeLinear','RequantizeLinear','Relu'):out=x
 else:raise RuntimeError('Unhandled '+op)
 for output in n.output:
  shapes[output]=out
  if output in expected and out!=expected[output]:
   row={'index':i,'name':n.name.decode(),'op':op,'inputs':ins,'actual':out,'exported':expected[output]};mismatches.append(row);print('MISMATCH',json.dumps(row))
else:
 completed=True
 print('All nodes reached')
report={'passed':completed and not mismatches,'nodes_checked':i+1,'node_count':len(g.node),'mismatches':mismatches}
args.out.write_text(json.dumps(report,indent=2))
if not report['passed']:raise SystemExit('Runtime shape audit FAILED')
