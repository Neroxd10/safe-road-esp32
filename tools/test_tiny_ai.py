"""Synthetic pipeline tests only. These are not camera-accuracy measurements."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import random
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('tiny_ai', Path(__file__).with_name('tiny_ai.py'))
ai = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ai)


def dataset():
    rng=random.Random(42)
    rows=[]
    for session in range(3):
        for _, eyes, mouth, _ in ai.POSES:
            for i in range(12):
                def vector(label,n):
                    return [(1 if label else -1)+rng.uniform(-0.12,0.12) for _ in range(n)]
                rows.append(dict(schema=1,boot_id=1,sample_id=len(rows)+1,
                                 session=f'round-{session}',eyes_closed=eyes,mouth_open=mouth,
                                 left_eye=vector(eyes,160),right_eye=vector(eyes,160),mouth=vector(mouth,192)))
    return rows


class PipelineTests(unittest.TestCase):
    def test_sigmoid_and_sign(self):
        self.assertAlmostEqual(ai.sigmoid(0),0.5)
        self.assertLess(ai.sigmoid(-1000),0.001)
        self.assertGreater(ai.sigmoid(1000),0.999)

    def test_validation_and_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)
            data=base/'samples.jsonl'
            data.write_text(''.join(json.dumps(r)+'\n' for r in dataset()),encoding='utf-8')
            args=SimpleNamespace(data=data,header=base/'weights.h',report=base/'report.json',epochs=50)
            with contextlib.redirect_stdout(io.StringIO()):
                ai.train(args)
            report=json.loads(args.report.read_text())
            self.assertTrue(report['passed'])
            self.assertEqual(report['parameter_bytes'],2060)
            self.assertEqual(report['train_samples'],36)
            self.assertEqual(report['validation_samples'],36)
            self.assertEqual(report['test_samples'],36)
            self.assertIn('trained = true',args.header.read_text())
            self.assertIn('mouth_weights[]',args.header.read_text())
            # Constant uninformative features must fail without replacing weights.
            old=args.header.read_bytes()
            rows=dataset()
            for row in rows:
                for feature in ('left_eye','right_eye','mouth'):
                    row[feature]=[0.0]*len(row[feature])
            data.write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError,'Validation gate failed'):
                ai.train(args)
            self.assertEqual(args.header.read_bytes(),old)

    def test_invalid_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'data.jsonl'
            rows=dataset()
            for mutate, message in [
                (lambda r:r.append(r[0]), 'Duplicate'),
                (lambda r:r[0].update(schema=99), 'schema'),
                (lambda r:r[0]['mouth'].__setitem__(0,float('nan')), 'feature'),
            ]:
                rows=dataset()
                mutate(rows)
                path.write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf-8')
                with self.assertRaisesRegex(ValueError,message):
                    ai.load_rows(path)

    def test_endpoint_request_has_key(self):
        response=io.BytesIO(b'{"schema":1}')
        with patch.object(ai.urllib.request,'urlopen',return_value=response) as call:
            result=ai.request_sample('http://device/','test-key')
        req=call.call_args.args[0]
        self.assertEqual(req.full_url,'http://device/api/training/sample')
        self.assertEqual(req.get_method(),'POST')
        self.assertEqual(json.loads(req.data),{'key':'test-key'})
        self.assertEqual(result,{'schema':1})


if __name__=='__main__':
    unittest.main()
