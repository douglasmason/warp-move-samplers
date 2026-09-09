import json, pathlib
r=pathlib.Path(__file__).parents[1]
for p in (r/'modules').glob('*/module.json'):
    d=json.load(open(p)); assert d['api_version']==2; assert d['capabilities']['component_type']=='sound_generator'; print('manifest ok',d['id'])
