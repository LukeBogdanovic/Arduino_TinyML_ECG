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

# Pre-install Zephyr core for Arduino Uno
RUN arduino-cli core install arduino:zephyr

# Clone the project repo
RUN git clone https://github.com/LukeBogdanovic/Arduino_TinyML_ECG.git /project

# Write a helper script to parse sketch.yaml and install Arduino libraries
RUN cat > /tmp/install_libs.py << 'PYEOF'
import yaml
import subprocess
import sys

with open('/project/sketch/sketch.yaml') as f:
    data = yaml.safe_load(f)

libs = data.get('dependencies', [])
if not libs:
    print("No dependencies found in sketch.yaml")
sys.exit(0)

for lib in libs:
    name = lib['name']
    version = lib.get('version', None)
    pkg = f"{name}@{version}" if version else name
    print(f"Installing: {pkg}")
    result = subprocess.run(['arduino-cli', 'lib', 'install', pkg], capture_output=True, text=True)
    print(result.stdout)
    if result.returncode != 0:
        print(result.stderr)
        sys.exit(result.returncode)
PYEOF

# Update library index and install libraries from sketch.yaml
RUN arduino-cli lib update-index && python3 /tmp/install_libs.py

# Install Python dependencies from requirements.txt into a venv
RUN python3 -m venv /venv \
    && /venv/bin/pip install --upgrade pip \
    && /venv/bin/pip install -r /project/python/requirements.txt

# Make venv the default python environment
ENV PATH="/venv/bin:$PATH"

# Set working directory to the project
WORKDIR /project

CMD ["/bin/bash"]