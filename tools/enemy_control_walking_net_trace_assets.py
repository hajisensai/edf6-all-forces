"""Read actual DSGO assets for the optional Walking production-wire trace fixture.
No game code runs; expression cadences remain unresolved raw bytecode metadata.
"""
from pathlib import Path
import argparse,hashlib,json,math,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'pylib'),str(ROOT/'tools')]
import dsgo,edf41_objects,rootcpk
parser=argparse.ArgumentParser()
parser.add_argument('--game',default=str(rootcpk.DEFAULT_GAME))
parser.add_argument('--output',type=Path,default=ROOT/'tmp/walking-net-trace-assets.tsv')
args=parser.parse_args()
G=rootcpk.Game(args.game)
output=args.output
assets=[]
for name in G.names('OBJECT'):
 if name.upper().startswith('E602_BERSERKER') and name.upper().endswith('.SGO'):
  raw=G.read('OBJECT',name);root=dsgo.parse(raw).root
  family=root.get('xgs_scene_object_class')
  if family in ('EDF6_Berserker_Large','EDF6_Berserker_Middle','EDF6_Berserker_TypeShooter'):assets.append((name,raw,root,family))
for row in edf41_objects.registry()['objects']:
 if row.get('class')=='EDF6_Berserker_Large':
  raw=edf41_objects.read(row['object']);assets.append((row['object'],raw,dsgo.parse(raw).root,row['class']))
assert len(assets)==36,len(assets)
def value(v):
 if isinstance(v,dsgo.Blob):return {'nativeNodeType':v.kind,'bytecode':v.data.hex(),'evaluated':False}
 if isinstance(v,dsgo.Node):return [value(x) for x in v.items]
 return v
def integer(v):return isinstance(v,float) and math.isfinite(v) and v>=0 and v==int(v)
records=[]
for asset,raw,root,family in sorted(assets,key=lambda x:x[0]):
 rows=root.get('gun_control').items if family=='EDF6_Berserker_Large' else [None]
 ranges=None
 try:ranges=root.get('edf41_hector').get('ranges').items
 except KeyError:pass
 configs=[];eligible=True
 for key,row in enumerate(rows):
  b=row.get('gun_battery') if row else root.get('gun_battery_settings')
  offset=row.get('interval') if row else 0.0
  charge=b.get('charge').get('time');count=b.get('fire').get('count');interval=b.get('fire').get('interval');shape=b.get('muzzle').get('shape')
  muzzle_count=len(shape.items) if isinstance(shape,dsgo.Node) else 1
  shield=ranges is not None and ranges[key]==0.0
  literals=integer(offset) and integer(charge) and integer(count) and integer(interval) and interval>=1 and count>=1
  if not shield and not literals:eligible=False
  configs.append({'ordinal':key,'offsetUpdates':value(offset),'chargeUpdates':value(charge),'fireCount':value(count),'intervalUpdates':value(interval),'muzzleCount':muzzle_count,'factory':b.get('bullet').get('factory'),'shieldPlaceholder':shield,'literalCadence':literals})
 scenario=Path(asset).stem.lower();meta={'record':'scenario','scenario':scenario,'sourceAsset':asset,'assetSha256':hashlib.sha256(raw).hexdigest(),'nativeClass':family,'guns':configs,'traceEligible':eligible,'clock':'native per-battery UpdateContext count; no Hz assumption','cadenceEvidence':'finite configured burst only; no MAB repeat or cooldown inferred','allocation':'original factory ABI fixture seam; production UnitFactory/Capture/Encode; independent admitted definition mock'}
 records.append((meta,configs,0))
 if Path(asset).name=='E602_BERSERKER_LARGE_ACE.SGO':
  variant=dict(meta);variant['scenario']=scenario+'_null17';variant['nullableFactory']={'fixtureEveryNthAttempt':17,'nullResultProducesNoAcceptedShot':True};records.append((variant,configs,17))
with output.open('w',encoding='utf-8',newline='\n') as f:
 for meta,configs,null_every in records:
  f.write('M\t'+json.dumps(meta,ensure_ascii=True,separators=(',',':'))+'\n')
  f.write('S\t'+meta['scenario']+'\t'+meta['nativeClass']+'\t'+str(int(meta['traceEligible']))+'\t'+str(null_every)+'\n')
  if meta['traceEligible']:
   for c in configs:
    if not c['shieldPlaceholder']:f.write('G\t'+'\t'.join(str(int(c[k])) for k in ('ordinal','offsetUpdates','chargeUpdates','fireCount','intervalUpdates','muzzleCount'))+'\n')
  f.write('E\n')
print(f'{len(assets)} actual assets / {len(records)} scenarios / {sum(m[0]["traceEligible"] for m in records)} literal-cadence traces -> {output}')
