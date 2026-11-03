"""Personalized ESP32 classifiers. Python standard library only; no cloud upload."""
import argparse
import getpass
import hashlib
import json
import math
from pathlib import Path
import time
import urllib.error
import urllib.request
import uuid

SCHEMA = 1  # normalized 16x10 eyes, 16x12 mouth; firmware vflip=1
POSES = [('neutral', 0, 0, 'Face forward, eyes OPEN, mouth CLOSED'),
         ('eyes_closed', 1, 0, 'Face forward, BOTH eyes CLOSED, mouth CLOSED'),
         ('mouth_open', 0, 1, 'Face forward, eyes OPEN, mouth WIDE OPEN')]


def request_sample(url, key):
    req = urllib.request.Request(url.rstrip('/') + '/api/training/sample',
                                 data=json.dumps({'key': key}).encode(),
                                 headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=12) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        try:
            detail = json.loads(exc.read()).get('error', str(exc))
        except (ValueError, UnicodeError):
            detail = str(exc)
        raise RuntimeError(detail) from exc


def collect(args):
    key = args.key or getpass.getpass('Device setup key (hotspot password): ')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    print('Collect while seated and stationary. No images leave the device: normalized crops only.')
    print('Keep camera fixed. Between rounds vary lighting slightly and sit back into position.')
    print('Three separate rounds are needed for training, validation and final testing.')
    run = str(uuid.uuid4())
    seen = set()
    with args.out.open('a', encoding='utf-8') as dest:
        for round_no in range(args.rounds):
            for name, eyes, mouth, prompt in POSES:
                input(f'\nRound {round_no+1}/{args.rounds}: {prompt}. Press Enter when ready. ')
                print('Capture starts in 4 seconds. Hold until the completion beep/message.')
                time.sleep(4)
                count = 0
                deadline = time.monotonic() + 90
                while count < args.samples:
                    if time.monotonic() > deadline:
                        raise RuntimeError('Capture timed out. Check face framing and connection, then rerun. Existing samples are kept.')
                    try:
                        sample = request_sample(args.url, key)
                    except RuntimeError as exc:
                        if 'key' in str(exc).lower():
                            raise
                        print(f'Waiting: {exc}')
                        time.sleep(0.5)
                        continue
                    ident = (sample['boot_id'], sample['sample_id'])
                    if ident in seen:
                        time.sleep(0.15)
                        continue
                    seen.add(ident)
                    if sample.get('schema') != SCHEMA:
                        raise RuntimeError('Feature schema mismatch: update tool and firmware together.')
                    sample.update(run=run, session=f'{run}:{round_no}', pose=name,
                                  eyes_closed=eyes, mouth_open=mouth)
                    dest.write(json.dumps(sample, separators=(',', ':')) + '\n')
                    dest.flush()
                    count += 1
                    print(f'\rCaptured {count}/{args.samples}', end='', flush=True)
                    time.sleep(0.15)
                print('\a\nDone. Relax before the next pose.')
    print(f'Saved samples: {args.out.resolve()}')


def sigmoid(z):
    return 1.0 / (1.0 + math.exp(-max(-40.0, min(40.0, z))))


def predict(model, x):
    return sigmoid(sum(w * v for w, v in zip(model['weights'], x)) + model['bias'])


def fit(rows, feature, target, epochs):
    n = len(rows[0][feature])
    weights, bias = [0.0] * n, 0.0
    groups = [[r for r in rows if r[target] == label] for label in (0, 1)]
    if not all(groups):
        raise ValueError('Both classes are required in each split.')
    # Deterministic balanced full-batch logistic regression. The input crops
    # are already contrast-normalized by the exact firmware preprocessing.
    for epoch in range(epochs):
        grad, gb = [0.0] * n, 0.0
        for group in groups:
            scale = 0.5 / len(group)
            for row in group:
                x = row[feature]
                error = (sigmoid(sum(w*v for w, v in zip(weights, x)) + bias) - row[target]) * scale
                gb += error
                for j, value in enumerate(x):
                    grad[j] += error * value
        lr = 0.15 / math.sqrt(1 + epoch / 20)
        weights = [w - lr*(g + 0.005*w) for w, g in zip(weights, grad)]
        bias -= lr * gb
    return dict(weights=weights, bias=bias)


def metrics(model, rows, feature, target):
    result = {}
    for label in (0, 1):
        predictions = [predict(model, r[feature]) for r in rows if r[target] == label]
        if not predictions:
            raise ValueError('Both classes are required in each split.')
        result[str(label)] = {
            'count': len(predictions),
            'accuracy': sum((p >= 0.5) == bool(label) for p in predictions) / len(predictions),
            'confident_correct': sum(p >= 0.8 if label else p <= 0.2 for p in predictions) / len(predictions),
        }
    return result


def load_rows(path):
    rows = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.strip()]
    if not rows:
        raise ValueError('No samples. Run collect first.')
    seen = set()
    for row in rows:
        if row.get('schema') != SCHEMA or not isinstance(row.get('session'), str):
            raise ValueError('Invalid sample schema/session.')
        ident = (row['boot_id'], row['sample_id'])
        if ident in seen:
            raise ValueError('Duplicate camera frame in dataset.')
        seen.add(ident)
        for key, width in [('left_eye', 160), ('right_eye', 160), ('mouth', 192)]:
            values = row.get(key, [])
            if len(values) != width or any(not isinstance(v, (float, int)) or not math.isfinite(v) or abs(v)>30 for v in values):
                raise ValueError(f'Invalid feature vector: {key}')
        for key in ('eyes_closed', 'mouth_open'):
            if row.get(key) not in (0, 1):
                raise ValueError(f'Invalid label: {key}')
    return rows


def render_header(models):
    out = ['// Generated by tools/tiny_ai.py; personalized experimental classifiers.',
           '#pragma once', 'namespace tiny_ai {', 'inline constexpr bool trained = true;',
           'inline constexpr int schema = 1;']
    def number(value):
        return f'{value:.9e}f'
    for name, model in models.items():
        out.append(f'inline constexpr float {name}_bias = {number(model["bias"])};')
        out.append(f'inline constexpr float {name}_weights[] = {{')
        for i in range(0, len(model['weights']), 8):
            out.append('    ' + ', '.join(map(number, model['weights'][i:i+8])) + ',')
        out.append('};')
    out.append('}')
    return '\n'.join(out) + '\n'


def train(args):
    rows = load_rows(args.data)
    sessions = list(dict.fromkeys(r['session'] for r in rows))
    if len(sessions) < 3:
        raise ValueError('Need at least 3 separate complete capture rounds; adjacent frames are not an independent test.')
    train_rows = [r for r in rows if r['session'] not in sessions[-2:]]
    val_rows = [r for r in rows if r['session'] == sessions[-2]]
    test_rows = [r for r in rows if r['session'] == sessions[-1]]
    specs = [('left_eye', 'eyes_closed'), ('right_eye', 'eyes_closed'), ('mouth', 'mouth_open')]
    models, report, passed = {}, {}, True
    for feature, target in specs:
        model = fit(train_rows, feature, target, args.epochs)
        models[feature] = model
        validation = metrics(model, val_rows, feature, target)
        test = metrics(model, test_rows, feature, target)
        report[feature] = dict(validation=validation, test=test)
        passed &= all(c['count'] >= 12 and c['accuracy'] >= 0.8 and c['confident_correct'] >= 0.7
                      for split in (validation, test) for c in split.values())
    paired = {}
    for name, split in [('validation', val_rows), ('test', test_rows)]:
        paired[name] = {}
        for label in (0, 1):
            subset = [r for r in split if r['eyes_closed'] == label]
            confident = 0
            for row in subset:
                left = predict(models['left_eye'], row['left_eye'])
                right = predict(models['right_eye'], row['right_eye'])
                confident += (left >= 0.8 and right >= 0.8) if label else (left <= 0.2 and right <= 0.2)
            rate = confident / len(subset)
            paired[name][str(label)] = dict(count=len(subset), confident_correct=rate)
            passed &= rate >= 0.7
    report.update(schema=SCHEMA, passed=bool(passed), train_samples=len(train_rows),
                  validation_samples=len(val_rows), test_samples=len(test_rows),
                  paired_eyes=paired, dataset_sha256=hashlib.sha256(args.data.read_bytes()).hexdigest(),
                  training_sessions=sessions[:-2], validation_session=sessions[-2], test_session=sessions[-1],
                  epochs=args.epochs,
                  parameter_bytes=(160+160+192+3)*4,
                  limitations='Personalized short-session experiment; not evidence of V7 equivalence or real-world accuracy.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    if not passed:
        raise ValueError('Validation gate failed. Existing firmware header was NOT changed. Collect more varied samples and retry.')
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(render_header(models), encoding='utf-8')
    print(f'Wrote {args.header.resolve()}. Rebuild and flash to activate. No PC/browser required afterward.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    c = subs.add_parser('collect')
    c.add_argument('--url', default='http://192.168.4.1')
    c.add_argument('--key', help='Omit to enter password without displaying it')
    c.add_argument('--out', type=Path, default=Path('training/samples.jsonl'))
    c.add_argument('--rounds', type=int, default=3)
    c.add_argument('--samples', type=int, default=24)
    t = subs.add_parser('train')
    t.add_argument('--data', type=Path, default=Path('training/samples.jsonl'))
    t.add_argument('--header', type=Path, default=Path('main/tiny_ai_weights.h'))
    t.add_argument('--report', type=Path, default=Path('training/report.json'))
    t.add_argument('--epochs', type=int, default=160)
    args = parser.parse_args()
    try:
        if args.command == 'collect':
            if args.rounds < 3 or args.samples < 12:
                raise ValueError('Use at least 3 rounds and 12 samples per pose.')
            collect(args)
        else:
            if args.epochs < 1:
                raise ValueError('Epochs must be positive.')
            train(args)
    except (ValueError, RuntimeError, OSError, KeyboardInterrupt) as exc:
        parser.exit(1, f'\nStopped: {exc}\n')


if __name__ == '__main__':
    main()
