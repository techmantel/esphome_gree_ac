FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

# Keep image lean while still having everything needed for ESPHome builds.
RUN python -m pip install --no-cache-dir --upgrade pip \
    && python -m pip install --no-cache-dir esphome