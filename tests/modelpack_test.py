"""pylib/modelpack.py: the split share package's model version and the installer's model check."""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import modelpack  # noqa: E402


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


with tempfile.TemporaryDirectory() as app:
    assert modelpack.problem(app) is None            # no models-required.json: a nightly zip, not checked
    models = os.path.join(app, 'models')
    write(os.path.join(models, 'a', 'x.bin'), b'1234')
    write(os.path.join(models, 'cockpits', 'parts', 'v', 'p.bin'), b'xy')
    m = modelpack.manifest(models)
    assert sorted(m['files']) == ['a/x.bin', 'cockpits/parts/v/p.bin'], m
    assert modelpack.manifest(models)['version'] == m['version']       # stable
    write(os.path.join(app, modelpack.REQUIRED), json.dumps({'models_version': m['version']}).encode())
    assert 'MODELS' not in (modelpack.problem(app) or '') and modelpack.problem(app)   # models/ has no manifest yet
    write(os.path.join(models, modelpack.MANIFEST), json.dumps(m).encode())
    assert modelpack.problem(app) is None
    assert modelpack.manifest(models)['version'] == m['version']       # the manifest is not part of the version
    os.remove(os.path.join(models, 'a', 'x.bin'))
    assert 'x.bin' in modelpack.problem(app)                            # incomplete
    write(os.path.join(models, 'a', 'x.bin'), b'1235')
    assert modelpack.manifest(models)['version'] != m['version']       # content changes the version
    write(os.path.join(models, modelpack.MANIFEST), json.dumps(modelpack.manifest(models)).encode())
    assert 'models' in modelpack.problem(app)                           # an older/newer pack than the installer wants
    write(os.path.join(models, modelpack.MANIFEST), b'{"version": "%s"}' % m['version'].encode())
    assert '损坏' in modelpack.problem(app)                             # a manifest without files: message, no KeyError
    write(os.path.join(models, modelpack.MANIFEST), json.dumps(modelpack.manifest(models)).encode())
    write(os.path.join(app, modelpack.REQUIRED), json.dumps({'models_version': modelpack.manifest(models)['version']}).encode())
    assert modelpack.problem(app) is None
    os.environ['EDF6VC_MODELS'] = os.path.join(app, 'elsewhere')       # the models folder the installer really uses
    assert modelpack.problem(app) is not None
    del os.environ['EDF6VC_MODELS']
    write(os.path.join(models, modelpack.MANIFEST), b'{"version": "%s"}' % m['version'].encode())
    assert '损坏' in modelpack.problem(app)                             # a manifest without files: message, no KeyError
    write(os.path.join(models, modelpack.MANIFEST), json.dumps(modelpack.manifest(models)).encode())
    write(os.path.join(app, modelpack.REQUIRED), json.dumps({'models_version': modelpack.manifest(models)['version']}).encode())
    assert modelpack.problem(app) is None
    os.environ['EDF6VC_MODELS'] = os.path.join(app, 'elsewhere')       # the models folder the installer really uses
    assert modelpack.problem(app) is not None
    del os.environ['EDF6VC_MODELS']
print('modelpack_test: ok')
