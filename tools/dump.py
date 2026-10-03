import sys,os
sys.path.insert(0,r'D:\APP\edf6-aa-flak\tools')
sys.path.insert(0,r'D:\APP\edf-coop-stable-multislot\multislot\tools')
os.environ.setdefault('EDF6_DIR',r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6')
import gamefs,sgo
for n in sys.argv[1:]:
    print('=====',n)
    v=sgo.load(data=gamefs.read('OBJECT',n))
    for k,x in v.items():
        s=repr(x)
        print(' ',k,'=',s[:400])
