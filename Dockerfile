FROM debian:unstable-slim

RUN apt update && apt install -y --no-install-recommends  \
  ca-certificates  \
  cmake  \
  gdb  \
  git  \
  g++-13  \
  make  \
  && apt -y clean && rm -rf /var/lib/apt/lists/*

WORKDIR /repo
