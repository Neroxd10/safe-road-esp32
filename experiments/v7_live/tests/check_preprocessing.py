"""Public-image preprocessing smoke test using the actual C++ sampling code and original V7 ONNX models."""
from pathlib import Path
import json,re,subprocess,math,argparse
import numpy as np
from PIL import Image,ImageDraw
import onnxruntime as ort
base=Path(__file__).resolve().parent
root=base.parents[2]
models=root/'experiments/v7_direct/models'
exe=base/'geometry_test.exe'
parser=argparse.ArgumentParser();parser.add_argument('--image',type=Path,default=base/'portrait.jpg');args=parser.parse_args();stem=args.image.stem
im=Image.open(args.image).convert('RGB');im.save(base/(stem+'.ppm'));w,h=im.size
subprocess.run([str(exe),str(base/(stem+'.ppm')),str(base/(stem+'_detector_input.bin'))],check=True)
x=np.fromfile(base/(stem+'_detector_input.bin'),np.float32).reshape(1,128,128,3)
s=ort.InferenceSession(str(models/'face_detector.onnx'),providers=['CPUExecutionProvider'])
y=s.run(None,{s.get_inputs()[0].name:x.transpose(0,3,1,2).copy()})
boxes=next(v.reshape(896,16) for v in y if v.size==896*16);logits=next(v.reshape(896) for v in y if v.size==896);score=1/(1+np.exp(-np.clip(logits,-40,40)))
detections=[];side=max(w,h);offset=np.array([(w-side)/2,(h-side)/2])
for i in np.flatnonzero(score>=.65):
 grid,repeats=(16,2) if i<512 else (8,6);cell=(int(i) if i<512 else int(i)-512)//repeats
 a=np.array([(cell%grid+.5)/grid,(cell//grid+.5)/grid]);b=boxes[i]
 detections.append((score[i],np.r_[((b[:2]/128+a)*side+offset),b[2:4]/128*side,((b[4:6]/128+a)*side+offset),((b[6:8]/128+a)*side+offset)]))
assert detections,'No face detected: preprocessing/model mismatch'
detections.sort(key=lambda d:-d[0]);best=detections[0][1]
def iou(a,b):
 lo=np.maximum(a[:2]-a[2:4]/2,b[:2]-b[2:4]/2);hi=np.minimum(a[:2]+a[2:4]/2,b[:2]+b[2:4]/2);inter=np.prod(np.maximum(0,hi-lo));return inter/(a[2]*a[3]+b[2]*b[3]-inter)
cluster=[d for d in detections if iou(best,d[1])>=.3];merged=sum(s*b for s,b in cluster)/sum(s for s,b in cluster)
cx,cy,bw,bh,ex0,ey0,ex1,ey1=merged
if ex0>ex1:ex0,ey0,ex1,ey1=ex1,ey1,ex0,ey0
roi=[float(cx),float(cy),float(1.5*max(bw,bh)),float(math.atan2(ey1-ey0,ex1-ex0))]
subprocess.run([str(exe),str(base/(stem+'.ppm')),str(base/(stem+'_landmark_input.bin')),*map(str,roi)],check=True)
x=np.fromfile(base/(stem+'_landmark_input.bin'),np.float32).reshape(1,256,256,3)
s=ort.InferenceSession(str(models/'face_landmarks_detector.onnx'),providers=['CPUExecutionProvider']);outputs=s.run(None,{s.get_inputs()[0].name:x.transpose(0,3,1,2).copy()});raw=next(v.reshape(478,3) for v in outputs if v.size==1434)
presence_logit=next(v for n,v in zip(s.get_outputs(),outputs) if n.name=='Identity_1').item();confidence=1/(1+math.exp(-max(-40,min(40,presence_logit))))
assert confidence>.6,('Landmark face presence failed',confidence)
uv=(raw[:,:2]/256-.5)*roi[2];rotation=np.array([[math.cos(roi[3]),-math.sin(roi[3])],[math.sin(roi[3]),math.cos(roi[3])]])
points=uv@rotation.T+np.array(roi[:2]);text=(root/'experiments/v7_live/main/v7_math.hpp').read_text();subset=list(map(int,re.search(r'subset\[146\]=\{(.*?)\};',text,re.S).group(1).replace('\n','').split(',')));assert len(subset)==146
s=ort.InferenceSession(str(models/'face_blendshapes.onnx'),providers=['CPUExecutionProvider']);blend=s.run(None,{s.get_inputs()[0].name:points[subset].astype(np.float32).reshape(1,146,2)})[0].reshape(52)
def dist(a,b):return float(np.linalg.norm(points[a]-points[b]))
def ear(ids):a,b,c,d,e,f=ids;return (dist(b,f)+dist(c,e))/(2*dist(a,d))
report={'source_image':'https://storage.googleapis.com/mediapipe-assets/'+args.image.name,'scope':'C++ image sampling plus original full-precision ONNX models on PC; not ESP32 quantized accuracy', 'image_size':[w,h],'detector_score':float(detections[0][0]),'landmark_confidence':confidence,'roi':roi,'ear_left':ear([33,160,158,133,153,144]),'ear_right':ear([362,385,387,263,373,380]),'mouth_ratio':dist(13,14)/dist(78,308),'blink_left':float(blend[9]),'blink_right':float(blend[10]),'jaw_open':float(blend[25])}
assert all(math.isfinite(report[k]) for k in ['ear_left','ear_right','mouth_ratio','blink_left','blink_right','jaw_open'])
(base/(stem+'_preprocessing_report.json')).write_text(json.dumps(report,indent=2));draw=ImageDraw.Draw(im)
for x,y in points:draw.ellipse((x-1,y-1,x+1,y+1),fill=(0,255,160))
im.save(base/(stem+'_landmarks.png'));print(json.dumps(report,indent=2))
