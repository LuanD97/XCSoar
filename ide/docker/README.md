# XCSoar Docker Image

This Docker Image when built, will compile XCSoar for several targets in a clean room environment.

## Currently Supported Targets

Targets:
  - UNIX (linux & co)
  - UNIX-SDL (Software Rendering for X11 forward)
  - ANDROID
  - PC
  - KOBO
  - DOCS

## Instructions

The container itself is readonly. The build results will appear in `./output/`.

To run the container interactivly:
```bash
docker run \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar \
    -it ghcr.io/xcsoar/xcsoar/xcsoar-build:latest /bin/bash
```

To run the ANDROID build:
```bash
docker run \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar \
    -it ghcr.io/xcsoar/xcsoar/xcsoar-build:latest xcsoar-compile ANDROID
```

To build the container:
```bash
docker build \
    --file ide/docker/Dockerfile \
    -t xcsoar/xcsoar-build:latest ./ide/
```

### Running XCSoar as a GUI application from the container

Sometimes your runtime environment diverges too far from the build environment to be able to execute the binary natively.
In this case you can start XCSoar inside the container and let it be displayed on your X11 Server:
```bash
docker run \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar \
    --volume="$HOME/.Xauthority:/root/.Xauthority:rw" \
    -v $HOME/.xcsoar/:/root/.xcsoar \
    --env="DISPLAY" --net=host \
    -it ghcr.io/xcsoar/xcsoar/xcsoar-build:latest /bin/bash
```
Compile and run the binary (UNIX-SDL target):
```bash
xcsoar-compile UNIX-SDL
./output/UNIX/bin/xcsoar
```

### Using ccache

Just add `USE_CCACHE=y` to the `xcsoar-compile` or `make` command (as you would do if compiling locally).
By default, `ccache` stores its data in `/root/.ccache`.

With the provided `docker-compose.yml`, that directory is backed by the named
Docker volume `xcsoar-ccache`, so the cache is shared across container
instances without putting lots of small cache files on the host bind mount.

## WSL / Docker Compose Setup

For WSL users, a `docker-compose.yml` is provided for easier workflow management with X11 forwarding support.

### Prerequisites

1. Docker Desktop installed on Windows with WSL 2 integration enabled
2. XCSoar source code cloned to `c:\Users\<username>\Documents\coding\XCSoar`
3. X11 server running on WSL (e.g., X410, VcXsrv, or WSLg built-in)

### Building the Docker Image

From the `ide/docker/` directory:

```bash
docker compose build
```

### Running Make Commands

To compile XCSoar for the Unix target:

```bash
cd ide/docker
docker compose run --rm xcsoar-dev bash -c "cd /opt/xcsoar && make TARGET=UNIX -j4"
```

To compile with specific targets:

```bash
# UNIX-SDL target (with SDL rendering)
docker compose run --rm xcsoar-dev bash -c "cd /opt/xcsoar && make TARGET=UNIX-SDL -j4"

# Android target
docker compose run --rm xcsoar-dev bash -c "cd /opt/xcsoar && make TARGET=ANDROID -j4"
```

### Running XCSoar GUI with X11 Forwarding

After building with X11 support, you can run XCSoar with graphical display:

1. Set DISPLAY environment variable (WSL users):
```bash
# In PowerShell or bash terminal on Windows
$env:DISPLAY = "127.0.0.1:0"  # or use your X11 server IP
```

2. Configure X11 access (WSL users):
```bash
xhost +local:docker
```

3. Start the container and run XCSoar:
```bash
cd ide/docker
docker compose run --rm xcsoar-dev bash -c "cd /opt/xcsoar && ./output/UNIX/bin/xcsoar"
```

### Interactive Shell

For development and debugging, open an interactive shell:

```bash
cd ide/docker
docker compose run --rm xcsoar-dev bash
```

Then inside the container:
```bash
cd /opt/xcsoar
make TARGET=UNIX -j4  # Build
./output/UNIX/bin/xcsoar  # Run with X11 forwarding
```

### Important Notes

- **Line Endings**: The `.gitattributes` file ensures all shell and Python scripts use Unix line endings (LF) when checked out on Windows
- **Volume Mounts**: Source code is mounted from the host at `/opt/xcsoar` inside the container
- **ccache**: Build cache persists across container runs in the
  `xcsoar-ccache` Docker volume mounted at `/root/.ccache`
- **X11 Forwarding**: Only available when DISPLAY environment variable is set and X server is accessible
