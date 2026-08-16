import pytest
from fastapi.testclient import TestClient

from app import main as main_module
from app.config import Settings
from app.models import ApiEnvelope, DataStatus, ErrorDetail


client = TestClient(main_module.app)


def assert_envelope(body: dict, expected_status: str = "fresh") -> None:
    assert body["schema_version"] == "1.0"
    assert isinstance(body["revision"], int)
    assert body["generated_at"].endswith(("Z", "+08:00"))
    assert body["status"] == expected_status


def test_healthz() -> None:
    response = client.get("/healthz")
    assert response.status_code == 200
    body = response.json()
    assert_envelope(body)
    assert body["data"]["mode"] == "mock"


def test_bootstrap_lists_all_pages_in_order() -> None:
    response = client.get("/v1/bootstrap")
    assert response.status_code == 200
    body = response.json()
    assert_envelope(body)
    pages = body["data"]["pages"]
    expected_ids = [
        "info", "calendar", "weather", "pve", "nas",
        "antigravity", "home", "album", "alerts", "settings",
    ]
    assert [page["id"] for page in pages] == expected_ids
    assert [page["order"] for page in pages] == list(range(len(expected_ids)))


def test_snapshot_contains_every_section_and_unsupported_antigravity() -> None:
    response = client.get("/v1/snapshot")
    assert response.status_code == 200
    body = response.json()
    assert_envelope(body)
    expected = {
        "clock", "info", "calendar", "weather", "pve", "nas",
        "antigravity", "home", "album", "alerts", "settings",
    }
    assert set(body["data"]) == expected
    assert body["data"]["weather"]["status"] == "fresh"
    antigravity = body["data"]["antigravity"]
    assert antigravity["status"] == "unsupported"
    assert antigravity["data"]["integration"] == "experimental"
    assert antigravity["error"]["code"] == "machine_readable_api_unavailable"


def test_tick_increments_revision() -> None:
    before = client.get("/v1/snapshot").json()["revision"]
    response = client.post("/v1/mock/tick")
    assert response.status_code == 200
    body = response.json()
    assert_envelope(body)
    assert body["data"]["previous_revision"] == before
    assert body["revision"] == before + 1


def test_home_command_is_confirmed_and_idempotent() -> None:
    command = {
        "request_id": "test-command-001",
        "entity_id": "light.living_room",
        "action": "set_brightness",
        "parameters": {"brightness_percent": 41},
    }
    first = client.post("/v1/home/commands", json=command)
    assert first.status_code == 200
    first_body = first.json()
    assert_envelope(first_body)
    assert first_body["data"]["request_id"] == command["request_id"]
    assert first_body["data"]["confirmed"] is True
    assert first_body["data"]["duplicate"] is False

    duplicate = client.post("/v1/home/commands", json=command)
    assert duplicate.status_code == 200
    assert duplicate.json()["data"]["duplicate"] is True
    assert duplicate.json()["revision"] == first_body["revision"]


def test_home_command_rejects_request_id_reuse_for_different_command() -> None:
    request_id = "test-command-conflict"
    first = client.post("/v1/home/commands", json={
        "request_id": request_id,
        "entity_id": "switch.study_socket",
        "action": "turn_on",
    })
    assert first.status_code == 200

    conflict = client.post("/v1/home/commands", json={
        "request_id": request_id,
        "entity_id": "switch.study_socket",
        "action": "turn_off",
    })
    assert conflict.status_code == 400
    assert conflict.json()["error"]["code"] == "request_id_conflict"


def test_home_command_rejects_non_whitelisted_entity() -> None:
    response = client.post("/v1/home/commands", json={
        "request_id": "test-command-denied",
        "entity_id": "lock.front_door",
        "action": "unlock",
    })
    assert response.status_code == 400
    body = response.json()
    assert_envelope(body, "error")
    assert body["error"]["code"] == "entity_not_allowed"


def test_home_command_validates_parameter_range() -> None:
    response = client.post("/v1/home/commands", json={
        "request_id": "test-command-invalid-temp",
        "entity_id": "climate.living_room",
        "action": "set_temperature",
        "parameters": {"temperature_c": 35},
    })
    assert response.status_code == 400
    assert response.json()["error"]["code"] == "invalid_parameters"


def test_request_validation_error_uses_standard_envelope() -> None:
    response = client.post("/v1/home/commands", json={
        "request_id": "contains spaces",
        "entity_id": "light.living_room",
        "action": "toggle",
        "unexpected": True,
    })
    assert response.status_code == 422
    body = response.json()
    assert_envelope(body, "error")
    assert body["error"]["code"] == "request_validation_error"


def test_command_rejects_unexpected_parameters() -> None:
    response = client.post("/v1/home/commands", json={
        "request_id": "test-command-extra-parameter",
        "entity_id": "switch.study_socket",
        "action": "turn_on",
        "parameters": {"brightness_percent": 100},
    })
    assert response.status_code == 400
    assert response.json()["error"]["code"] == "invalid_parameters"


def test_write_token_protects_mutating_endpoints(monkeypatch) -> None:
    protected = Settings(api_token="test-token-at-least-16-characters")
    monkeypatch.setattr(main_module, "settings", protected)

    missing = client.post("/v1/mock/tick")
    assert missing.status_code == 401
    assert_envelope(missing.json(), "error")
    assert missing.json()["error"]["code"] == "authentication_required"

    accepted = client.post(
        "/v1/mock/tick",
        headers={"X-API-Token": protected.api_token},
    )
    assert accepted.status_code == 200
    assert_envelope(accepted.json())


def test_not_found_uses_standard_envelope() -> None:
    response = client.get("/does-not-exist")
    assert response.status_code == 404
    assert_envelope(response.json(), "error")
    assert response.json()["error"]["code"] == "not_found"


def test_envelope_status_invariants() -> None:
    now = main_module.state.now()
    try:
        ApiEnvelope(
            revision=1,
            generated_at=now,
            status=DataStatus.fresh,
            data=None,
        )
    except ValueError:
        pass
    else:
        raise AssertionError("fresh envelope without data must be rejected")

    valid_error = ApiEnvelope(
        revision=1,
        generated_at=now,
        status=DataStatus.error,
        error=ErrorDetail(code="example", message="example"),
    )
    assert valid_error.status == DataStatus.error


def test_required_auth_fails_closed_without_token(monkeypatch) -> None:
    monkeypatch.setenv("GATEWAY_REQUIRE_AUTH", "true")
    monkeypatch.delenv("GATEWAY_API_TOKEN", raising=False)
    with pytest.raises(ValueError, match="GATEWAY_API_TOKEN is required"):
        Settings.from_env()
