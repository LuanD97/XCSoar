FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y python3 vim ccache python3-pkgconfig gdb && apt-get clean
COPY ./provisioning/install-debian-packages.sh /root/install-debian-packages.sh
RUN sed -i 's/\r$//' /root/install-debian-packages.sh && chmod +x /root/install-debian-packages.sh && /root/install-debian-packages.sh && apt-get clean
COPY ./provisioning/install-android-tools.sh /root/install-android-tools.sh
RUN sed -i 's/\r$//' /root/install-android-tools.sh && chmod +x /root/install-android-tools.sh && /root/install-android-tools.sh && apt-get clean

COPY ./docker/bin/* /usr/local/bin/
WORKDIR /usr/local/bin
RUN chmod 755 ./*

# Copy entire source code and fix line endings for all shell and Python scripts
COPY . /opt/xcsoar
WORKDIR /opt/xcsoar
RUN find . -type f \( -name "*.py" -o -name "*.sh" -o -name "Makefile" -o -name "*.mk" \) -exec sed -i 's/\r$//' {} \; && \
    find . -type f \( -name "*.sh" -o -name "Makefile" -o -name "*.mk" \) -exec chmod +x {} \;

ENV CCACHE_DIR=/root/.ccache
