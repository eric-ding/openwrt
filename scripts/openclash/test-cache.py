"""GitHub-hosted root-only fixture: public fake assets, no proxy/network."""
import gzip,hashlib,os,shutil,subprocess,tempfile
from pathlib import Path
assert os.environ.get('GITHUB_ACTIONS')=='true'
assert os.geteuid()==0
script=str(Path('scripts/openclash/core-cache.sh').resolve())
with tempfile.TemporaryDirectory(prefix='k3-cache-test-') as temp:
    base=Path(temp);runtime=base/'tmp/etc/openclash';persist=base/'etc/openclash';cache=persist/'k3-cache'
    (runtime/'core').mkdir(parents=True);persist.mkdir(parents=True)
    core=b'synthetic binary fixture\0'*10000
    (runtime/'core/clash_meta').write_bytes(core);(runtime/'GeoSite.dat').write_bytes(b'public synthetic database')
    (runtime/'subscription.yaml').write_text('must never enter cache')
    env=dict(os.environ,K3_CACHE_TEST_ROOT=temp)
    def run(action,valid=True):
        r=subprocess.run(['sh',script,action],env=env,text=True,capture_output=True,timeout=30)
        assert (r.returncode==0)==valid,(action,r.returncode,r.stdout,r.stderr)
    run('sync')
    assert {p.name for p in cache.iterdir()}=={'clash_meta','GeoSite.dat'}
    digest=hashlib.sha256(core).hexdigest();folder=cache/'clash_meta'
    assert (folder/'current.sha256').read_text().strip()==digest
    shutil.rmtree(runtime);run('restore')
    assert (runtime/'core/clash_meta').read_bytes()==core
    assert (runtime/'core/clash_meta').stat().st_mode&0o777==0o700
    assert (runtime/'GeoSite.dat').read_bytes()==b'public synthetic database'
    run('restore')
    newer=b'next verified public fixture'*20000
    (runtime/'core/clash_meta').write_bytes(newer);run('sync')
    newdigest=hashlib.sha256(newer).hexdigest()
    assert not (folder/(digest+'.gz')).exists()
    assert (folder/'current.sha256').read_text().strip()==newdigest
    # Orphaned interrupted update cannot change the committed selection.
    (folder/'uncommitted.gz.new').write_bytes(b'interrupted')
    shutil.rmtree(runtime);run('restore')
    assert (runtime/'core/clash_meta').read_bytes()==newer
    # Corruption must fail closed, rather than restoring a partial executable.
    (folder/(newdigest+'.gz')).write_bytes(gzip.compress(b'corrupt different bytes'))
    shutil.rmtree(runtime);run('restore',False)
    assert not (runtime/'core/clash_meta').exists()
    # Invalid selector and symlink inputs must be rejected.
    (folder/'current.sha256').write_text('../../outside\n');run('restore',False)
    (folder/'current.sha256').unlink();(folder/'current.sha256').symlink_to('/etc/passwd');run('restore',False)
print('Compressed cache checks passed: whitelist, reboot restore, atomic selection, upgrade, permissions, corruption and symlink rejection')
