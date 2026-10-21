"""Cloud-only root fixture. No real service, WAN, credentials or EAPOL."""
import json,os,signal,stat,subprocess,sys,time
from pathlib import Path
root=Path('/tmp/njit-rpc-fixture')
root.mkdir(mode=0o700)
(root/'client').write_bytes(Path('/tmp/njit-fixture-client').read_bytes())
(root/'client').chmod(0o700)
(root/'uci').mkdir(mode=0o700)
(root/'runtime').mkdir(mode=0o700)
(root/'service.json').write_text('{}')
(root/'carrier').write_text('1\n')
(root/'ubus').write_text('''#!/usr/bin/python3
import sys
from pathlib import Path
r=Path('/tmp/njit-rpc-fixture')
if sys.argv[4]=='service': print((r/'service.json').read_text())
else: print('{"up":true,"ipv4-address":[{"address":"192.0.2.10"}],"password":"must-not-return","subscription":"secret-url"}')
''')
(root/'service').write_text('''#!/usr/bin/python3
import sys
from pathlib import Path
r=Path('/tmp/njit-rpc-fixture')
with (r/'actions').open('a') as f: f.write(sys.argv[1]+'\\n')
''')
for name in ['ubus','service']:(root/name).chmod(0o700)
helper=sys.argv[1]
secret="synthetic only ' $= password"
def call(method,args=None,raw=None):
    r=subprocess.run([helper,'call',method],input=raw if raw is not None else json.dumps(args or {}),text=True,capture_output=True,timeout=8)
    assert r.returncode==0,(method,r.returncode,r.stderr)
    assert not r.stderr and secret not in r.stdout
    return json.loads(r.stdout)
def ok(method,args=None):
    r=call(method,args);assert r['ok'],r;return r
schema=json.loads(subprocess.check_output([helper,'list'],text=True))
assert set(schema)=={'status','get_settings','save','save_options','clear','control'}
r=ok('get_settings');assert not r['password_set'] and not r['autostart'] and not r['reconnect']
assert call('save',{'username':'u','domain':'','password':''})['error']=='PASSWORD_REQUIRED'
assert call('save',{'username':'u','domain':'','password':'x','path':'/tmp/injected'})['error']=='INVALID_INPUT'
for invalid in ['[]','{} trailing','{"password":"x"}', '{"username":"a\\u0000b","password":"x","domain":""}']:
    assert not call('save',raw=invalid)['ok']
assert not call('save',{'username':'u\n','domain':'','password':'p'})['ok']
assert not call('save',{'username':'u'*88,'domain':'d','password':'p'})['ok']
ok('save',{'username':"literal $=' 用户",'domain':'campus','password':secret})
f=root/'private/credentials';assert stat.S_IMODE(f.stat().st_mode)==0o600
assert stat.S_IMODE(f.parent.stat().st_mode)==0o700
r=ok('get_settings');assert r['username']=="literal $=' 用户" and r['domain']=='campus' and r['password_set'] and 'password' not in r
ok('save',{'username':'changed','domain':'','password':''});assert secret in f.read_text()
assert not (root/'actions').exists(),'saving credentials must not start a service'
for a,r in [(False,False),(True,False),(False,True),(True,True)]:
    ok('save_options',{'autostart':a,'reconnect':r})
    s=ok('get_settings');assert s['autostart']==a and s['reconnect']==r
    flags=json.loads(subprocess.check_output([helper,'options'],text=True));assert flags=={'autostart':a,'reconnect':r}
assert not (root/'actions').exists(),'options alone must not launch a process'
assert not call('save_options',{'autostart':1,'reconnect':False})['ok']
s=ok('status');assert s['service_known'] and not s['running'] and s['carrier'] and s['ipv4_addresses']==['192.0.2.10']
assert 'must-not-return' not in json.dumps(s) and 'secret-url' not in json.dumps(s)
assert call('control',{'action':'start;touch /tmp/injected'})['error']=='INVALID_ACTION'
ok('control',{'action':'start'});assert (root/'actions').read_text()=='start\n'
assert "option enabled '1'" in (root/'uci/njitclient').read_text()
child=subprocess.Popen([str(root/'client')])
try:
    (root/'service.json').write_text(json.dumps({'njitclient':{'instances':{'main':{'running':True,'pid':child.pid}}}}))
    (root/'runtime/state.json').write_text(json.dumps({'state':'authenticated','pid':child.pid,'monotonic_seconds':int(time.monotonic()),'password':secret}))
    (root/'runtime/state.json').chmod(0o600)
    s=ok('status');assert s['running'] and s['event']=='authenticated' and 'password' not in s
    assert call('save',{'username':'u','domain':'','password':'new'})['error']=='STOP_BEFORE_SAVE'
    assert call('clear')['error']=='STOP_BEFORE_SAVE'
    (root/'runtime/state.json').write_text(json.dumps({'state':'authenticated','pid':child.pid+1,'monotonic_seconds':0}))
    assert 'event' not in ok('status'),'must suppress stale PID events'
finally:
    child.terminate();child.wait(timeout=3)
(root/'service.json').write_text('{}')
ok('control',{'action':'stop'});assert "option enabled '0'" in (root/'uci/njitclient').read_text()
ok('clear');assert not f.exists() and not ok('get_settings')['password_set']
(root/'private/options.json').write_text('{malformed')
s=ok('get_settings');assert not s['options_valid'] and not s['autostart'] and not s['reconnect']
assert json.loads(subprocess.check_output([helper,'options'],text=True))=={'autostart':False,'reconnect':False}
print('RPC privacy, atomic credentials, option combinations, fixed controls and status correlation checks passed')
