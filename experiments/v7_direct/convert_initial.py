from pathlib import Path
import zipfile,json,traceback
import tflite2onnx,onnx
base=Path('experiments/v7_direct');(base/'models').mkdir(exist_ok=True)
bundle=Path('C:/Users/Administrator/Music/SAFE_ROAD_SD_OFFLINE_V7_DROWSINESS/SAFE_ROAD_SD_OFFLINE_V7_DROWSINESS/sdcard/models/face_landmarker.task')
results={}
with zipfile.ZipFile(bundle) as z:
 for name in ('face_landmarks_detector.tflite','face_detector.tflite','face_blendshapes.tflite'):
  src=base/'models'/name;src.write_bytes(z.read(name));out=src.with_suffix('.onnx')
  try:
   tflite2onnx.convert(str(src),str(out));onnx.checker.check_model(str(out))
   results[name]={'converted':True,'onnx_bytes':out.stat().st_size}
  except Exception as e:
   results[name]={'converted':False,'error':str(e)};traceback.print_exc(limit=3)
(base/'conversion_initial.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
