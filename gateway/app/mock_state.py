from __future__ import annotations

import calendar as month_calendar
import copy
import threading
from datetime import date, datetime, timedelta
from typing import Any
from zoneinfo import ZoneInfo

from .config import Settings
from .models import (
    AlbumData,
    AlertItem,
    AlertsData,
    AntigravityData,
    CalendarData,
    CalendarDay,
    ClockData,
    DataStatus,
    ErrorDetail,
    ForecastDay,
    HolidayItem,
    HomeCommandAck,
    HomeCommandRequest,
    HomeData,
    HomeEntity,
    InfoData,
    NasData,
    NasDisk,
    NasVolume,
    NewsHeadline,
    PhotoItem,
    PveData,
    PveGuest,
    PveNode,
    SectionEnvelope,
    SettingsData,
    SnapshotData,
    ProviderResult,
    TickResult,
    WeatherData,
)


class CommandRejected(ValueError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


class MockState:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.tz = ZoneInfo(settings.timezone)
        self.started_at = self.now()
        self.revision = 1
        self._lock = threading.RLock()
        self._command_history: dict[str, tuple[HomeCommandRequest, HomeCommandAck]] = {}
        self._entities = self._initial_entities()

    def now(self) -> datetime:
        return datetime.now(self.tz)

    @property
    def uptime_seconds(self) -> int:
        return max(0, int((self.now() - self.started_at).total_seconds()))

    @property
    def current_revision(self) -> int:
        with self._lock:
            return self.revision

    def _initial_entities(self) -> dict[str, HomeEntity]:
        entities = [
            HomeEntity(
                entity_id="light.living_room",
                name="客厅主灯",
                room="客厅",
                domain="light",
                state="on",
                attributes={"brightness_percent": 72},
                allowed_actions=["turn_on", "turn_off", "toggle", "set_brightness"],
            ),
            HomeEntity(
                entity_id="climate.living_room",
                name="客厅空调",
                room="客厅",
                domain="climate",
                state="cool",
                attributes={"temperature_c": 24, "current_temperature_c": 27.2},
                allowed_actions=["turn_on", "turn_off", "set_temperature", "set_mode"],
            ),
            HomeEntity(
                entity_id="cover.living_room",
                name="客厅窗帘",
                room="客厅",
                domain="cover",
                state="open",
                attributes={"position_percent": 100},
                allowed_actions=["open", "close", "set_position"],
            ),
            HomeEntity(
                entity_id="switch.study_socket",
                name="书房插座",
                room="书房",
                domain="switch",
                state="off",
                allowed_actions=["turn_on", "turn_off", "toggle"],
            ),
        ]
        return {entity.entity_id: entity for entity in entities}

    def snapshot(self) -> SnapshotData:
        with self._lock:
            now = self.now()
            today = now.date()
            return SnapshotData(
                clock=self._section(ClockData(
                    local_time=now,
                    timezone=self.settings.timezone,
                    utc_offset=now.strftime("%z")[:3] + ":" + now.strftime("%z")[3:],
                ), now, 2),
                info=self._section(self._info(now), now, 300),
                calendar=self._section(self._calendar(today), now, 21600),
                weather=self._section(self._weather(today), now, 600),
                pve=self._section(self._pve(), now, 30),
                nas=self._section(self._nas(), now, 60),
                antigravity=SectionEnvelope(
                    status=DataStatus.unsupported,
                    updated_at=now,
                    data=AntigravityData(),
                    error=ErrorDetail(
                        code="machine_readable_api_unavailable",
                        message=(
                            "Antigravity quota is experimental: no supported machine-readable "
                            "quota API has been configured."
                        ),
                        retryable=False,
                    ),
                ),
                home=self._section(HomeData(entities=copy.deepcopy(list(self._entities.values()))), now, 30),
                alerts=self._section(self._alerts(now), now, 30),
                settings=self._section(self._settings(), now, 300),
            )

    def snapshot_with_revision(self) -> tuple[int, SnapshotData]:
        with self._lock:
            return self.revision, self.snapshot()

    @staticmethod
    def _section(data: Any, now: datetime, stale_after_seconds: int) -> SectionEnvelope[Any]:
        return SectionEnvelope(
            status=DataStatus.fresh,
            updated_at=now,
            stale_after_seconds=stale_after_seconds,
            data=data,
        )

    def _info(self, now: datetime) -> InfoData:
        return InfoData(
            stock=ProviderResult(
                status=DataStatus.unsupported,
                error=ErrorDetail(
                    code="stock_provider_unconfigured",
                    message="LongPort stock credentials are not configured.",
                    retryable=False,
                ),
            ),
            fuel=ProviderResult(
                status=DataStatus.unsupported,
                error=ErrorDetail(
                    code="fuel_provider_unconfigured",
                    message="ShowAPI fuel credentials are not configured.",
                    retryable=False,
                ),
            ),
            headlines=[],
            disclaimer="Live providers are not configured; no fabricated market values are shown.",
        )

    def _calendar(self, today: date) -> CalendarData:
        count = month_calendar.monthrange(today.year, today.month)[1]
        days = []
        for day_number in range(1, count + 1):
            day_date = date(today.year, today.month, day_number)
            days.append(CalendarDay(
                date=day_date,
                day=day_number,
                weekday=day_date.isoweekday(),
                is_weekend=day_date.isoweekday() >= 6,
            ))

        return CalendarData(
            year=today.year,
            month=today.month,
            today=today,
            lunar_today="农历七月初二（Mock）",
            days=days,
            upcoming=[
                HolidayItem(date=date(2026, 9, 25), name="中秋节（Mock）", kind="holiday"),
                HolidayItem(date=date(2026, 10, 1), name="国庆节（Mock）", kind="holiday"),
            ],
            disclaimer="Holiday and adjusted-workday entries are mock placeholders, not official schedules.",
        )

    def _weather(self, today: date) -> WeatherData:
        temperature = 29.0 + ((self.revision % 3) - 1) * 0.2
        return WeatherData(
            city=self.settings.city,
            condition="多云",
            icon="partly_cloudy",
            temperature_c=round(temperature, 1),
            feels_like_c=32.1,
            humidity_percent=76,
            wind="东南风 2级",
            precipitation_percent=35,
            air_quality_index=42,
            air_quality_label="优",
            forecast=[
                ForecastDay(date=today, label="今天", condition="多云", icon="partly_cloudy", low_c=26, high_c=32, precipitation_percent=35),
                ForecastDay(date=today + timedelta(days=1), label="明天", condition="阵雨", icon="rain", low_c=25, high_c=30, precipitation_percent=65),
                ForecastDay(date=today + timedelta(days=2), label="后天", condition="晴间多云", icon="sunny", low_c=26, high_c=33, precipitation_percent=20),
            ],
        )

    def _pve(self) -> PveData:
        cpu = 18.2 + (self.revision % 4) * 0.7
        return PveData(
            nodes=[PveNode(
                id="pve-1",
                name="pve-home",
                status="online",
                cpu_percent=round(cpu, 1),
                memory_used_gb=18.6,
                memory_total_gb=64,
                storage_used_gb=816,
                storage_total_gb=1900,
                uptime_seconds=1_284_312 + self.uptime_seconds,
            )],
            guests=[
                PveGuest(vmid=100, name="home-assistant", kind="qemu", status="running", cpu_percent=4.1, memory_used_mb=2300, memory_total_mb=4096, disk_used_gb=18.4, disk_total_gb=32),
                PveGuest(vmid=101, name="dashboard-gateway", kind="lxc", status="running", cpu_percent=1.3, memory_used_mb=420, memory_total_mb=1024, disk_used_gb=4.2, disk_total_gb=16),
                PveGuest(vmid=102, name="lab", kind="qemu", status="stopped", cpu_percent=0, memory_used_mb=0, memory_total_mb=8192, disk_used_gb=42, disk_total_gb=128),
            ],
        )

    def _nas(self) -> NasData:
        return NasData(
            hostname="diskstation",
            model="DS920+ (Mock)",
            uptime_seconds=2_510_000 + self.uptime_seconds,
            volumes=[NasVolume(id="volume1", name="主存储", status="healthy", used_tb=7.4, total_tb=14.5)],
            disks=[
                NasDisk(bay=1, model="ST4000VN008", status="healthy", temperature_c=36, size_tb=4),
                NasDisk(bay=2, model="ST4000VN008", status="healthy", temperature_c=37, size_tb=4),
                NasDisk(bay=3, model="ST4000VN008", status="healthy", temperature_c=38, size_tb=4),
                NasDisk(bay=4, model="ST4000VN008", status="warning", temperature_c=44, size_tb=4),
            ],
        )

    @staticmethod
    def _album(now: datetime) -> AlbumData:
        return AlbumData(
            album_name="家庭精选",
            current_index=0,
            photos=[
                PhotoItem(id="photo-001", title="海边日落", captured_at=now - timedelta(days=3), width=480, height=320, thumbnail_url="/mock-assets/photos/photo-001.jpg"),
                PhotoItem(id="photo-002", title="周末公园", captured_at=now - timedelta(days=8), width=480, height=320, thumbnail_url="/mock-assets/photos/photo-002.jpg"),
                PhotoItem(id="photo-003", title="城市夜景", captured_at=now - timedelta(days=15), width=480, height=320, thumbnail_url="/mock-assets/photos/photo-003.jpg"),
            ],
            note="Metadata only in v0.1; thumbnail URLs are placeholders until SMB thumbnailing is implemented.",
        )

    def _alerts(self, now: datetime) -> AlertsData:
        items = [
            AlertItem(
                id="nas-disk-4-hot",
                severity="warning",
                source="nas",
                title="硬盘 4 温度偏高",
                message="当前 44°C，建议检查机箱通风。",
                active=True,
                occurred_at=now - timedelta(minutes=12),
            ),
            AlertItem(
                id="gateway-mock-mode",
                severity="info",
                source="gateway",
                title="正在使用 Mock 数据",
                message="连接真实服务前，页面数据不会反映家庭环境。",
                active=True,
                occurred_at=self.started_at,
            ),
        ]
        return AlertsData(unread_count=sum(item.active for item in items), items=items)

    def _settings(self) -> SettingsData:
        return SettingsData(
            city=self.settings.city,
            timezone=self.settings.timezone,
            theme="dark",
            brightness_percent=78,
            wifi_configured=False,
            backend_mode="demo",
            ota_channel="stable",
        )

    def tick(self) -> TickResult:
        with self._lock:
            previous = self.revision
            self.revision += 1
            return TickResult(
                previous_revision=previous,
                revision=self.revision,
                changed_sections=["clock", "info", "weather", "pve", "nas", "alerts"],
            )

    def execute_home_command(self, request: HomeCommandRequest) -> tuple[int, HomeCommandAck]:
        with self._lock:
            previous = self._command_history.get(request.request_id)
            if previous is not None:
                previous_request, previous_ack = previous
                if previous_request != request:
                    raise CommandRejected(
                        "request_id_conflict",
                        "request_id was already used for a different command",
                    )
                return self.revision, previous_ack.model_copy(update={"duplicate": True})

            entity = self._entities.get(request.entity_id)
            if entity is None:
                raise CommandRejected("entity_not_allowed", "Entity is not in the command whitelist")
            if request.action not in entity.allowed_actions:
                raise CommandRejected("action_not_allowed", "Action is not allowed for this entity")

            self._apply_command(entity, request.action, request.parameters)
            self.revision += 1
            ack = HomeCommandAck(
                request_id=request.request_id,
                confirmed=True,
                entity_id=entity.entity_id,
                action=request.action,
                resulting_state=entity.state,
                accepted_at=self.now(),
            )
            self._command_history[request.request_id] = (request.model_copy(deep=True), ack)
            if len(self._command_history) > 128:
                oldest = next(iter(self._command_history))
                del self._command_history[oldest]
            return self.revision, ack

    @staticmethod
    def _number(parameters: dict[str, Any], name: str, minimum: float, maximum: float) -> float:
        value = parameters.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise CommandRejected("invalid_parameters", f"{name} must be a number")
        if not minimum <= value <= maximum:
            raise CommandRejected("invalid_parameters", f"{name} must be between {minimum} and {maximum}")
        return float(value)

    def _apply_command(self, entity: HomeEntity, action: str, parameters: dict[str, Any]) -> None:
        allowed_parameters = {
            "toggle": set(),
            "turn_on": set(),
            "turn_off": set(),
            "open": set(),
            "close": set(),
            "set_brightness": {"brightness_percent"},
            "set_temperature": {"temperature_c"},
            "set_position": {"position_percent"},
            "set_mode": {"mode"},
        }
        unexpected = set(parameters) - allowed_parameters.get(action, set())
        if unexpected:
            names = ", ".join(sorted(unexpected))
            raise CommandRejected("invalid_parameters", f"Unexpected parameters for {action}: {names}")

        if action == "toggle":
            entity.state = "off" if entity.state == "on" else "on"
        elif action == "turn_on":
            entity.state = "on"
        elif action == "turn_off":
            entity.state = "off"
        elif action == "open":
            entity.state = "open"
            entity.attributes["position_percent"] = 100
        elif action == "close":
            entity.state = "closed"
            entity.attributes["position_percent"] = 0
        elif action == "set_brightness":
            value = self._number(parameters, "brightness_percent", 1, 100)
            entity.state = "on"
            entity.attributes["brightness_percent"] = int(value)
        elif action == "set_temperature":
            value = self._number(parameters, "temperature_c", 16, 30)
            entity.attributes["temperature_c"] = round(value, 1)
        elif action == "set_position":
            value = self._number(parameters, "position_percent", 0, 100)
            entity.attributes["position_percent"] = int(value)
            entity.state = "closed" if value == 0 else "open"
        elif action == "set_mode":
            mode = parameters.get("mode")
            if mode not in {"cool", "heat", "fan_only", "dry", "auto"}:
                raise CommandRejected("invalid_parameters", "mode is not allowed")
            entity.state = str(mode)
        else:
            raise CommandRejected("action_not_implemented", "Allowed action has no mock implementation")
