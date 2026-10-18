"""No network or real credentials: registry loading and voice-only configuration."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import sys

spec=importlib.util.spec_from_file_location('cosmo_bootstrap',Path(__file__).with_name('cosmo_service.py'))
service=importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

def clean(monkeypatch):
    for name in ('XAI_API_KEY','GROK_API_KEY','XAI_MODEL','COSMO_AI_PROVIDER','XAI_VOICE_MODEL','XAI_VOICE'):
        monkeypatch.delenv(name,raising=False)

def test_persistent_user_environment_loaded_without_exposing_key(monkeypatch):
    clean(monkeypatch)
    class Key:
        def __enter__(self): return self
        def __exit__(self,*args): pass
    def query(key,name):
        if name=='XAI_API_KEY': return 'fixture-key',1
        raise OSError()
    monkeypatch.setitem(sys.modules,'winreg',SimpleNamespace(HKEY_CURRENT_USER=1,HKEY_LOCAL_MACHINE=2,OpenKey=lambda *args:Key(),QueryValueEx=query))
    service.refresh_environment()
    assert service.os.environ['XAI_API_KEY']=='fixture-key'
    assert service.runtime_modes({'provider':'auto'})==('mock',True,True)

def test_process_environment_takes_precedence(monkeypatch):
    clean(monkeypatch)
    monkeypatch.setenv('XAI_API_KEY','process-fixture')
    service.refresh_environment()
    assert service.os.environ['XAI_API_KEY']=='process-fixture'

def test_alias_is_supported_without_requiring_text_model(monkeypatch):
    clean(monkeypatch)
    monkeypatch.setenv('GROK_API_KEY','alias-fixture')
    monkeypatch.setitem(sys.modules,'winreg',SimpleNamespace(HKEY_CURRENT_USER=1,HKEY_LOCAL_MACHINE=2,OpenKey=lambda *args:(_ for _ in ()).throw(OSError())))
    service.refresh_environment()
    assert service.runtime_modes({'provider':'auto'})==('mock',True,True)

def test_explicit_mock_never_contacts_grok(monkeypatch):
    clean(monkeypatch)
    monkeypatch.setenv('XAI_API_KEY','fixture-key')
    monkeypatch.setenv('COSMO_AI_PROVIDER','mock')
    assert service.runtime_modes({'provider':'auto'})==('mock',False,True)

def test_missing_key_is_reportable_not_an_endless_voice_connection(monkeypatch):
    clean(monkeypatch)
    assert service.runtime_modes({'provider':'auto'})==('mock',False,False)
