"""Run the compiled Manager scale path with deterministic success/failure stubs.

Complements native integer/float representability tests and compiled menu tests.
Does not emulate live Papyrus scheduling or game object/wire refresh.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, sys

spec = importlib.util.spec_from_file_location('component_payment', Path(__file__).with_name('Test-ClipboardComponentPayment.py'))
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

class World:
    stub_self = {'showfinisheddialogs', 'enterworkshopmodeifneeded'}
    def __init__(self, success, error):
        self.self_ref = module.Ref('manager')
        self.tool, self.menus = module.Ref('tool'), module.Ref('menus')
        self.fields = {'referenceobject': self.tool, '::clipboard_menus_var': self.menus,
                       '::selection_effect_list_var': module.Ref('effects'), 'effectindex': 0}
        self.success, self.error = success, error
        self.calls = []
        self.events = []
    def method(self, receiver, name, args):
        name = name.lower()
        self.calls.append((name, args))
        if name == 'showfinisheddialogs': return True
        if name == 'getat': return module.Ref('shader')
        if name in ('enterworkshopmodeifneeded', 'showinformmenu'): return None
        raise AssertionError((name, args))
    def static(self, owner, name, args):
        name = name.lower()
        self.calls.append((name, args))
        if name == 'getselectedobjectreferences': return [module.Ref('one'), module.Ref('two')]
        if name == 'tryscaleselection': return self.success
        if name == 'getselectionscaleerror': return self.error
        if name == 'gettext': return args[0]
        if name in ('wait','disableobjects','enableobjects','applyshadereffecttoselection',
                    'sendworkshopeventtoselectedobjects','updateselectedwires'): return None
        raise AssertionError((owner,name,args))

def run(path):
    manager=module.CompiledManager(path.read_text(encoding='utf-8-sig'))
    cases=[]
    for whole in (False,True):
        for workshop in (False,True):
            for factor in (1.12,.88,-1):
                for success,error in ((True,''),(False,'exact scale error'),(False,'')):
                    world=World(success,error)
                    manager.run(world,'ScaleSelected',[factor,whole,workshop])
                    names=[name for name,_ in world.calls]
                    assert 'scaleselection' not in names, 'legacy void call cannot report rejection'
                    assert names.count('tryscaleselection')==1
                    assert ('enterworkshopmodeifneeded' in names) == (not workshop)
                    refresh=['disableobjects','enableobjects','applyshadereffecttoselection',
                             'sendworkshopeventtoselectedobjects','updateselectedwires']
                    assert [name for name in names if name in refresh] == (refresh if success else [])
                    assert names.count('wait') == (5 if success else 0)
                    message=next(args[0] for name,args in world.calls if name=='showinformmenu')
                    assert message == ('$Clipboard_ScaleFinished' if success else error or '$Clipboard_ScaleSelectionInvalid')
                    assert names.index('tryscaleselection') < names.index('showinformmenu')
                    if success: assert names.index('tryscaleselection') < names.index('disableobjects')
                    cases.append(dict(whole=whole,workshop=workshop,factor=factor,success=success,error=error))
    return cases

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assembly',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    cases=run(args.assembly)
    report=dict(result='passed',caseCount=len(cases),cases=cases,
        assemblySha256=hashlib.sha256(args.assembly.read_bytes()).hexdigest().upper(),
        scope='Offline compiled control flow; no live scheduling or power certification.')
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"Passed {len(cases)} compiled scale control-flow cases.")
