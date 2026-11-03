"""Reproduce V7 conversion; does not modify or flash the main firmware."""
from pathlib import Path
import argparse,json,subprocess,sys,zipfile,shutil,hashlib
base=Path(__file__).resolve().parent
root=base.parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--bundle',type=Path,default=Path('C:/Users/Administrator/Music/SAFE_ROAD_SD_OFFLINE_V7_DROWSINESS/SAFE_ROAD_SD_OFFLINE_V7_DROWSINESS/sdcard/models/face_landmarker.task'))
p.add_argument('--quantize-smoke',action='store_true',help='Use SYNTHETIC calibration; structural test only')
args=p.parse_args()
(base/'models').mkdir(exist_ok=True)
with zipfile.ZipFile(args.bundle) as z:
 for stem in ('face_detector','face_landmarks_detector','face_blendshapes'):
  (base/'models'/f'{stem}.tflite').write_bytes(z.read(f'{stem}.tflite'))
def run(name,arguments):
 print(name,flush=True)
 with (base/(name+'.log')).open('w') as log:
  subprocess.run([sys.executable,*arguments],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
run('convert_detector',['-c',"import logging,tflite2onnx;logging.disable(logging.CRITICAL);tflite2onnx.convert('experiments/v7_direct/models/face_detector.tflite','experiments/v7_direct/models/face_detector.onnx')"])
run('convert_landmark',[str(base/'convert_landmark.py')])
run('convert_blendshapes_tf',['-m','tf2onnx.convert','--tflite',str(base/'models/face_blendshapes.tflite'),'--output',str(base/'models/face_blendshapes.onnx'),'--opset','13'])
run('normalize_blendshapes',[str(base/'normalize_blendshapes.py')])
run('validate_all',[str(base/'validate_all.py')])
if args.quantize_smoke:
 for stem in ('face_detector','face_landmarks_detector','face_blendshapes'):
  run('quantize_'+stem,[str(base/'quantize_smoke.py'),'--model',stem])
 run('audit_runtime_shapes',[str(base/'audit_runtime_shapes.py')])
 run('quantize_expression_diagnostic',[str(base/'quantize_smoke.py'),'--model','face_blendshapes','--diagnostic'])
 shutil.copyfile(base/'models/face_blendshapes_s3_int8_diagnostic.espdl',base/'benchmark/main/blendshapes_diagnostic.espdl')
 for source,target in [('face_detector_s3_int8_smoke','detector'),('face_landmarks_s3_int8_smoke','landmark'),('face_blendshapes_s3_int8_smoke','blendshapes')]:
  shutil.copyfile(base/'models'/f'{source}.espdl',base/'benchmark/main'/f'{target}.espdl')
print('Conversion and equivalence checks complete. Nothing flashed.')
