FROM debian:sid
RUN sed -i 's/Components: main/Components: main contrib/' /etc/apt/sources.list.d/debian.sources && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    mame mame-tools xvfb xauth xdotool imagemagick hfsutils python3 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
