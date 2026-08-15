FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    GATEWAY_REQUIRE_AUTH=true

WORKDIR /app

COPY requirements.lock ./
COPY app ./app
RUN pip install --no-cache-dir --requirement requirements.lock

USER nobody
EXPOSE 8080

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]
