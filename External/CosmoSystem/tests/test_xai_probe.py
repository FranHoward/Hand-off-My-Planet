"""No live API calls: verify diagnostics safely survive authentication errors."""
from io import BytesIO
import json
import sys
from urllib.error import HTTPError

import pytest

import test_xai as probe


@pytest.fixture
def probe_key(monkeypatch):
    key = "xai-offline-probe-secret"
    monkeypatch.setenv("XAI_API_KEY", key)
    monkeypatch.delenv("XAI_MODEL", raising=False)
    monkeypatch.setattr(sys, "argv", ["test_xai.py", "--list-models"])
    return key


@pytest.mark.parametrize("structured", [True, False])
def test_400_reports_reason_without_echoing_key(monkeypatch, capsys, probe_key, structured):
    detail = f"Incorrect API key provided: {probe_key}"
    body = json.dumps({"error": {"code": "bad-request", "message": detail}}).encode() if structured else detail.encode()
    def rejected(request, timeout):
        raise HTTPError(request.full_url, 400, "Bad Request", {}, BytesIO(body))
    monkeypatch.setattr(probe, "urlopen", rejected)
    assert probe.main() == 1
    output = capsys.readouterr().err
    assert "HTTP 400" in output and "Incorrect API key" in output
    assert "[REDACTED]" in output and probe_key not in output
    assert "does not use XAI_MODEL" in output


def test_list_models_keeps_auth_header_and_needs_no_model(monkeypatch, capsys, probe_key):
    def accepted(request, timeout):
        assert request.get_method() == "GET"
        assert request.get_header("Authorization") == f"Bearer {probe_key}"
        assert request.get_header("Accept") == "application/json"
        assert timeout > 0
        return BytesIO(b'{"data":[{"id":"configured-text-model"}]}')
    monkeypatch.setattr(probe, "urlopen", accepted)
    assert probe.main() == 0
    output = capsys.readouterr().out
    assert json.loads(output)["models"] == ["configured-text-model"]
    assert probe_key not in output


@pytest.mark.parametrize("invalid", ["Bearer xai-offline-probe-secret", '"xai-offline-probe-secret"', "System.Security.SecureString", "xai-offline\nprobe"])
def test_local_key_format_errors_never_send_request(monkeypatch, capsys, probe_key, invalid):
    monkeypatch.setenv("XAI_API_KEY", invalid)
    def unexpected(*args, **kwargs):
        pytest.fail("An invalid local credential must not be sent")
    monkeypatch.setattr(probe, "urlopen", unexpected)
    assert probe.main() == 2
    assert "invalid local format" in capsys.readouterr().err
