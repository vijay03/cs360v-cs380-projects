# Part 4: the container image you develop in.
#
# ubuntu:24.04 is multi-arch, so this builds natively on Apple Silicon or Intel.
FROM ubuntu:24.04

# A C compiler and Python 3.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        libc6-dev \
        make \
        python3 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Run as the image's non-root 'ubuntu' user. run.sh compiles to /tmp, not the
# mounted /work, so nothing the container writes touches your machine.
WORKDIR /work
USER ubuntu

# Keep the container alive so you can `docker compose exec` into it.
CMD ["sleep", "infinity"]
