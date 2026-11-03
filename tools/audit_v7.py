"""Read TFLite FlatBuffer metadata without installing TensorFlow or changing models."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import zipfile


class FB:
    def __init__(self, data):
        self.data = data

    def unpack(self, fmt, pos):
        return struct.unpack_from('<' + fmt, self.data, pos)[0]

    def field(self, table, slot):
        vt = table - self.unpack('i', table)
        off = 4 + slot * 2
        delta = self.unpack('H', vt + off) if off < self.unpack('H', vt) else 0
        return table + delta if delta else None

    def scalar(self, table, slot, fmt='i', default=0):
        pos = self.field(table, slot)
        return default if pos is None else self.unpack(fmt, pos)

    def vector(self, table, slot, objects=False):
        pos = self.field(table, slot)
        if pos is None:
            return []
        vec = pos + self.unpack('I', pos)
        n = self.unpack('I', vec)
        return [(vec+4+i*4+self.unpack('I', vec+4+i*4)) if objects
                else self.unpack('i', vec+4+i*4) for i in range(n)]


OPS = {0:'ADD',1:'AVERAGE_POOL_2D',2:'CONCATENATION',3:'CONV_2D',
       4:'DEPTHWISE_CONV_2D',6:'DEQUANTIZE',9:'FULLY_CONNECTED',14:'LOGISTIC',
       17:'MAX_POOL_2D',18:'MUL',19:'RELU',21:'RELU6',22:'RESHAPE',
       23:'RESIZE_BILINEAR',25:'SOFTMAX',28:'TANH',34:'PAD',36:'GATHER',
       39:'TRANSPOSE',40:'MEAN',41:'SUB',42:'DIV',45:'STRIDED_SLICE',
       49:'SPLIT',53:'CAST',55:'MAXIMUM',57:'MINIMUM',58:'LESS',59:'NEG',
       65:'SLICE',67:'TRANSPOSE_CONV',70:'EXP',71:'TOPK_V2',77:'SHAPE',
       83:'PACK',88:'UNPACK',90:'FLOOR_DIV',92:'SQUARE',94:'FILL',
       97:'RESIZE_NEAREST_NEIGHBOR',114:'QUANTIZE',117:'HARD_SWISH',
       126:'BATCH_MATMUL',150:'GELU'}
TYPES = {0:'FLOAT32',1:'FLOAT16',2:'INT32',3:'UINT8',4:'INT64',6:'BOOL',7:'INT16',9:'INT8'}


def audit(data):
    if data[4:8] != b'TFL3':
        raise ValueError('Not a TFLite FlatBuffer')
    fb=FB(data)
    model=fb.unpack('I',0)
    codes=[]
    for code in fb.vector(model,1,True):
        builtin=max(fb.scalar(code,0,'b'),fb.scalar(code,3))
        codes.append(OPS.get(builtin, f'BUILTIN_{builtin}'))
    graphs=[]
    for graph in fb.vector(model,2,True):
        tensors=fb.vector(graph,0,True)
        shape=lambda i: fb.vector(tensors[i],0)
        tensor=lambda i: {'shape':shape(i),'type':TYPES.get(fb.scalar(tensors[i],1,'b'), 'OTHER')}
        operators=fb.vector(graph,3,True)
        graphs.append({'inputs':[tensor(i) for i in fb.vector(graph,1)],
                       'outputs':[tensor(i) for i in fb.vector(graph,2)],
                       'tensor_types':dict(Counter(TYPES.get(fb.scalar(t,1,'b'),'OTHER') for t in tensors)),
                       'operators':dict(Counter(codes[fb.scalar(op,0,'I')] for op in operators))})
    return {'bytes':len(data),'subgraphs':graphs}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('model',type=Path)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    with zipfile.ZipFile(args.model) as bundle:
        result={name:audit(bundle.read(name)) for name in bundle.namelist() if name.endswith('.tflite')}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
