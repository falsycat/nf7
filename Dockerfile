FROM debian:unstable-slim

RUN apt update && apt install -y --no-install-recommends  \
  ca-certificates  \
  cmake  \
  gcc-13  \
  gdb  \
  git  \
  g++-13  \
  make  \
  && apt -y clean && rm -rf /var/lib/apt/lists/*

RUN  \
  ln -s /usr/bin/gcc-13 /usr/bin/gcc &&  \
  ln -s /usr/bin/g++-13 /usr/bin/g++

WORKDIR /repo
