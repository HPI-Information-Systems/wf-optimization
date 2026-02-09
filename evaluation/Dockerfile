FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y \
		ninja-build \
		gcc-14 \
		g++-14 \
		sudo \
		golang \
		python3 \
		python3-pip \
		python3-venv \
	pip3 install --break-system-packages scipy numpy pandas matplotlib seaborn palettable
