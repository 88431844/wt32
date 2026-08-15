from __future__ import annotations

import os
import warnings
from dataclasses import dataclass
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError


@dataclass(frozen=True)
class Settings:
    app_name: str = "WT32 Dashboard Gateway"
    host: str = "0.0.0.0"
    port: int = 8080
    timezone: str = "Asia/Shanghai"
    city: str = "深圳"
    log_level: str = "info"
    api_token: str = ""
    require_auth: bool = False

    @classmethod
    def from_env(cls) -> "Settings":
        timezone = os.getenv("GATEWAY_TIMEZONE", cls.timezone)
        try:
            ZoneInfo(timezone)
        except ZoneInfoNotFoundError as exc:
            raise ValueError(f"Unknown GATEWAY_TIMEZONE: {timezone}") from exc

        port = int(os.getenv("GATEWAY_PORT", str(cls.port)))
        if not 1 <= port <= 65535:
            raise ValueError("GATEWAY_PORT must be between 1 and 65535")

        require_auth_value = os.getenv("GATEWAY_REQUIRE_AUTH", "false").strip().lower()
        if require_auth_value not in {"true", "false", "1", "0", "yes", "no"}:
            raise ValueError("GATEWAY_REQUIRE_AUTH must be true or false")
        require_auth = require_auth_value in {"true", "1", "yes"}

        api_token = os.getenv("GATEWAY_API_TOKEN", "")
        if api_token and len(api_token) < 16:
            raise ValueError("GATEWAY_API_TOKEN must be at least 16 characters")
        if require_auth and not api_token:
            raise ValueError("GATEWAY_API_TOKEN is required when GATEWAY_REQUIRE_AUTH=true")
        if not require_auth and not api_token:
            warnings.warn(
                "GATEWAY_API_TOKEN is unset; mutating endpoints are unauthenticated",
                RuntimeWarning,
                stacklevel=2,
            )

        return cls(
            app_name=os.getenv("GATEWAY_APP_NAME", cls.app_name),
            host=os.getenv("GATEWAY_HOST", cls.host),
            port=port,
            timezone=timezone,
            city=os.getenv("GATEWAY_CITY", cls.city),
            log_level=os.getenv("GATEWAY_LOG_LEVEL", cls.log_level),
            api_token=api_token,
            require_auth=require_auth,
        )
