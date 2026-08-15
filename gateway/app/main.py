from __future__ import annotations

import secrets
from typing import Annotated, Any

from fastapi import Depends, FastAPI, Header, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

from . import __version__
from .config import Settings
from .mock_state import CommandRejected, MockState
from .models import (
    ApiEnvelope,
    BootstrapData,
    DataStatus,
    ErrorDetail,
    HealthData,
    HomeCommandAck,
    HomeCommandRequest,
    PageDefinition,
    SnapshotData,
    TickResult,
)


settings = Settings.from_env()
state = MockState(settings)

app = FastAPI(
    title=settings.app_name,
    version=__version__,
    description="Mock-first backend for the WT32-SC01 LVGL dashboard.",
)


class AuthenticationRejected(ValueError):
    pass


def envelope(
    data: Any,
    status: DataStatus = DataStatus.fresh,
    revision: int | None = None,
) -> ApiEnvelope[Any]:
    return ApiEnvelope(
        revision=state.current_revision if revision is None else revision,
        generated_at=state.now(),
        status=status,
        data=data,
    )


def error_response(status_code: int, code: str, message: str) -> JSONResponse:
    failure = ApiEnvelope[None](
        revision=state.current_revision,
        generated_at=state.now(),
        status=DataStatus.error,
        error=ErrorDetail(code=code, message=message, retryable=False),
    )
    return JSONResponse(status_code=status_code, content=failure.model_dump(mode="json"))


@app.exception_handler(RequestValidationError)
def validation_error_handler(_: Request, exc: RequestValidationError) -> JSONResponse:
    locations = [".".join(str(part) for part in error["loc"]) for error in exc.errors()]
    return error_response(
        422,
        "request_validation_error",
        "Invalid request fields: " + ", ".join(locations),
    )


@app.exception_handler(AuthenticationRejected)
def authentication_error_handler(_: Request, exc: AuthenticationRejected) -> JSONResponse:
    return error_response(401, "authentication_required", str(exc))


@app.exception_handler(StarletteHTTPException)
def http_error_handler(_: Request, exc: StarletteHTTPException) -> JSONResponse:
    code = "not_found" if exc.status_code == 404 else "http_error"
    return error_response(exc.status_code, code, str(exc.detail))


def require_write_token(
    x_api_token: Annotated[str | None, Header(alias="X-API-Token")] = None,
) -> None:
    expected = settings.api_token
    if not expected:
        return
    if x_api_token is None or not secrets.compare_digest(x_api_token, expected):
        raise AuthenticationRejected("A valid X-API-Token header is required")


def bootstrap_data() -> BootstrapData:
    definitions = [
        ("clock", "时间", False),
        ("info", "资讯", False),
        ("calendar", "日历", False),
        ("weather", "天气", False),
        ("pve", "PVE", False),
        ("nas", "群晖 NAS", False),
        ("antigravity", "Antigravity", False),
        ("home", "智能家居", True),
        ("album", "相册", True),
        ("alerts", "告警中心", True),
        ("settings", "设置", True),
    ]
    return BootstrapData(
        device_profile="wt32-sc01-v3.2-landscape",
        display_width=480,
        display_height=320,
        refresh_seconds=15,
        pages=[
            PageDefinition(id=page_id, title=title, order=index, source=page_id, interactive=interactive)
            for index, (page_id, title, interactive) in enumerate(definitions)
        ],
        capabilities=[
            "snapshot",
            "mock_tick",
            "idempotent_home_commands",
            "per_section_freshness",
        ],
    )


@app.get("/healthz", response_model=ApiEnvelope[HealthData])
def healthz() -> ApiEnvelope[HealthData]:
    return envelope(HealthData(
        service=settings.app_name,
        version=__version__,
        uptime_seconds=state.uptime_seconds,
    ))


@app.get("/v1/bootstrap", response_model=ApiEnvelope[BootstrapData])
def bootstrap() -> ApiEnvelope[BootstrapData]:
    return envelope(bootstrap_data())


@app.get("/v1/snapshot", response_model=ApiEnvelope[SnapshotData])
def snapshot() -> ApiEnvelope[SnapshotData]:
    revision, data = state.snapshot_with_revision()
    return envelope(data, revision=revision)


@app.post(
    "/v1/home/commands",
    response_model=ApiEnvelope[HomeCommandAck],
    dependencies=[Depends(require_write_token)],
)
def home_command(request: HomeCommandRequest) -> ApiEnvelope[HomeCommandAck] | JSONResponse:
    try:
        revision, result = state.execute_home_command(request)
    except CommandRejected as exc:
        return error_response(400, exc.code, str(exc))
    return envelope(result, revision=revision)


@app.post(
    "/v1/mock/tick",
    response_model=ApiEnvelope[TickResult],
    dependencies=[Depends(require_write_token)],
)
def mock_tick() -> ApiEnvelope[TickResult]:
    result = state.tick()
    return envelope(result, revision=result.revision)
