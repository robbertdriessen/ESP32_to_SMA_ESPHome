FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    HTTP_PORT=8080 \
    UDP_PORT=1514 \
    LOG_BUFFER=10000

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py ./

EXPOSE 8080/tcp
EXPOSE 1514/udp

ENTRYPOINT ["python", "-u", "/app/app.py"]
