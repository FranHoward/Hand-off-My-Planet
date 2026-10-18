import pytest


@pytest.fixture(autouse=True)
def local_provider_by_default(monkeypatch):
    # Tests never consume real API credit on a machine configured for Grok.
    monkeypatch.setenv("COSMO_AI_PROVIDER", "local")
