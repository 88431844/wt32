from __future__ import annotations

from datetime import date, datetime
from enum import Enum
from typing import Any, Generic, Literal, TypeVar

from pydantic import BaseModel, ConfigDict, Field, model_validator


SCHEMA_VERSION = "1.0"
T = TypeVar("T")


class DataStatus(str, Enum):
    fresh = "fresh"
    stale = "stale"
    error = "error"
    unsupported = "unsupported"


class ErrorDetail(BaseModel):
    code: str
    message: str
    retryable: bool = False


class ApiEnvelope(BaseModel, Generic[T]):
    schema_version: str = SCHEMA_VERSION
    revision: int = Field(ge=0)
    generated_at: datetime
    status: DataStatus
    data: T | None = None
    error: ErrorDetail | None = None

    @model_validator(mode="after")
    def validate_status_payload(self) -> "ApiEnvelope[T]":
        _validate_envelope(self.status, self.data, self.error)
        return self


class SectionEnvelope(BaseModel, Generic[T]):
    status: DataStatus
    updated_at: datetime
    stale_after_seconds: int | None = Field(default=None, ge=1)
    data: T | None = None
    error: ErrorDetail | None = None

    @model_validator(mode="after")
    def validate_status_payload(self) -> "SectionEnvelope[T]":
        _validate_envelope(self.status, self.data, self.error)
        return self


def _validate_envelope(status: DataStatus, data: Any, error: ErrorDetail | None) -> None:
    if status == DataStatus.fresh and (data is None or error is not None):
        raise ValueError("fresh envelopes require data and must not contain an error")
    if status == DataStatus.stale and data is None:
        raise ValueError("stale envelopes require cached data")
    if status in {DataStatus.error, DataStatus.unsupported} and error is None:
        raise ValueError(f"{status.value} envelopes require an error detail")


class HealthData(BaseModel):
    service: str
    version: str
    mode: Literal["mock"] = "mock"
    uptime_seconds: int = Field(ge=0)


class PageDefinition(BaseModel):
    id: str
    title: str
    order: int = Field(ge=0)
    source: str
    interactive: bool = False


class BootstrapData(BaseModel):
    device_profile: str
    display_width: int
    display_height: int
    orientation: Literal["landscape"] = "landscape"
    refresh_seconds: int = Field(ge=1)
    pages: list[PageDefinition]
    capabilities: list[str]


class ClockData(BaseModel):
    local_time: datetime
    timezone: str
    utc_offset: str


class StockQuote(BaseModel):
    symbol: str
    name: str
    currency: str
    price: float
    change: float
    change_percent: float
    market_status: str
    delayed: bool = True


class FuelPrice(BaseModel):
    grade: str
    unit: str = "CNY/L"
    price: float
    change: float = 0


class NewsHeadline(BaseModel):
    id: str
    title: str
    source: str
    published_at: datetime


class InfoData(BaseModel):
    stock: StockQuote
    fuel_prices: list[FuelPrice]
    headlines: list[NewsHeadline]
    disclaimer: str


class CalendarDay(BaseModel):
    date: date
    day: int = Field(ge=1, le=31)
    weekday: int = Field(ge=1, le=7)
    is_weekend: bool
    holiday_name: str | None = None
    is_workday_adjustment: bool = False


class HolidayItem(BaseModel):
    date: date
    name: str
    kind: Literal["holiday", "adjusted_workday"]


class CalendarData(BaseModel):
    year: int
    month: int = Field(ge=1, le=12)
    today: date
    lunar_today: str
    days: list[CalendarDay]
    upcoming: list[HolidayItem]
    disclaimer: str


class ForecastDay(BaseModel):
    date: date
    label: str
    condition: str
    icon: str
    low_c: int
    high_c: int
    precipitation_percent: int = Field(ge=0, le=100)


class WeatherData(BaseModel):
    city: str
    condition: str
    icon: str
    temperature_c: float
    feels_like_c: float
    humidity_percent: int = Field(ge=0, le=100)
    wind: str
    precipitation_percent: int = Field(ge=0, le=100)
    air_quality_index: int = Field(ge=0)
    air_quality_label: str
    forecast: list[ForecastDay]


class PveNode(BaseModel):
    id: str
    name: str
    status: Literal["online", "offline"]
    cpu_percent: float = Field(ge=0, le=100)
    memory_used_gb: float = Field(ge=0)
    memory_total_gb: float = Field(gt=0)
    storage_used_gb: float = Field(ge=0)
    storage_total_gb: float = Field(gt=0)
    uptime_seconds: int = Field(ge=0)


class PveGuest(BaseModel):
    vmid: int
    name: str
    kind: Literal["qemu", "lxc"]
    status: Literal["running", "stopped"]
    cpu_percent: float = Field(ge=0, le=100)
    memory_used_mb: int = Field(ge=0)
    memory_total_mb: int = Field(gt=0)
    disk_used_gb: float = Field(ge=0)
    disk_total_gb: float = Field(gt=0)


class PveData(BaseModel):
    nodes: list[PveNode]
    guests: list[PveGuest]


class NasVolume(BaseModel):
    id: str
    name: str
    status: Literal["healthy", "warning", "critical"]
    used_tb: float = Field(ge=0)
    total_tb: float = Field(gt=0)


class NasDisk(BaseModel):
    bay: int = Field(ge=1)
    model: str
    status: Literal["healthy", "warning", "critical"]
    temperature_c: int
    size_tb: float = Field(gt=0)


class NasData(BaseModel):
    hostname: str
    model: str
    uptime_seconds: int = Field(ge=0)
    volumes: list[NasVolume]
    disks: list[NasDisk]


class AntigravityData(BaseModel):
    integration: Literal["experimental"] = "experimental"
    quota_remaining_percent: float | None = None
    reset_at: datetime | None = None


class HomeEntity(BaseModel):
    entity_id: str
    name: str
    room: str
    domain: Literal["light", "switch", "climate", "cover"]
    state: str
    available: bool = True
    attributes: dict[str, Any] = Field(default_factory=dict)
    allowed_actions: list[str]


class HomeData(BaseModel):
    entities: list[HomeEntity]


class PhotoItem(BaseModel):
    id: str
    title: str
    captured_at: datetime
    width: int = Field(gt=0)
    height: int = Field(gt=0)
    thumbnail_url: str


class AlbumData(BaseModel):
    source: Literal["synology_smb_mock"] = "synology_smb_mock"
    album_name: str
    current_index: int = Field(ge=0)
    photos: list[PhotoItem]
    note: str


class AlertItem(BaseModel):
    id: str
    severity: Literal["info", "warning", "critical"]
    source: Literal["pve", "nas", "home", "weather", "gateway"]
    title: str
    message: str
    active: bool
    occurred_at: datetime


class AlertsData(BaseModel):
    unread_count: int = Field(ge=0)
    items: list[AlertItem]


class SettingsData(BaseModel):
    city: str
    timezone: str
    theme: Literal["dark", "light", "auto"]
    brightness_percent: int = Field(ge=5, le=100)
    wifi_configured: bool
    backend_mode: Literal["mock"] = "mock"
    ota_channel: Literal["stable", "beta"]


class SnapshotData(BaseModel):
    clock: SectionEnvelope[ClockData]
    info: SectionEnvelope[InfoData]
    calendar: SectionEnvelope[CalendarData]
    weather: SectionEnvelope[WeatherData]
    pve: SectionEnvelope[PveData]
    nas: SectionEnvelope[NasData]
    antigravity: SectionEnvelope[AntigravityData]
    home: SectionEnvelope[HomeData]
    album: SectionEnvelope[AlbumData]
    alerts: SectionEnvelope[AlertsData]
    settings: SectionEnvelope[SettingsData]


class HomeCommandRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    request_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._:-]+$")
    entity_id: str = Field(min_length=1, max_length=128)
    action: str = Field(min_length=1, max_length=32)
    parameters: dict[str, Any] = Field(default_factory=dict, max_length=4)


class HomeCommandAck(BaseModel):
    request_id: str
    confirmed: bool
    duplicate: bool = False
    entity_id: str
    action: str
    resulting_state: str
    accepted_at: datetime


class TickResult(BaseModel):
    previous_revision: int
    revision: int
    changed_sections: list[str]
