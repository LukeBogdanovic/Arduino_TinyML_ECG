# Arduino CLI Docker Container
# Base image: Ubuntu LTS
FROM ubuntu:24.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    curl \
    ca-certificates \
    git \
    python3 \
    python3-pip \
    python3-venv \
    python3-yaml \
    && rm -rf /var/lib/apt/lists/*

# Install Arduino CLI (latest version)
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh -o /tmp/install-arduino-cli.sh \
    && BINDIR=/usr/local/bin sh /tmp/install-arduino-cli.sh \
    && rm /tmp/install-arduino-cli.sh

# Initialize Arduino CLI config and update core index
RUN arduino-cli config init \
    && arduino-cli core update-index

# Pre-install Zephyr core for Arduino Uno Q
RUN arduino-cli core install arduino:zephyr

# Clone the project repo
RUN git clone https://github.com/LukeBogdanovic/Arduino_TinyML_ECG.git /home/ubuntu/project

RUN arduino-cli lib update-index

RUN arduino-cli lib install ArduinoFFT

RUN arduino-cli lib install Arduino_Routerbridge

# Install Python dependencies from requirements.txt into a venv
RUN python3 -m venv /venv \
    && /venv/bin/pip install --upgrade pip \
    && /venv/bin/pip install -r /project/python/requirements.txt

# Make venv the default python environment
ENV PATH="/venv/bin:$PATH"

# Set working directory to the project
WORKDIR /home/ubuntu/project

CMD ["/bin/bash"]