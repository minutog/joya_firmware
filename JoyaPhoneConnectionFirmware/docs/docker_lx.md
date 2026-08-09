# Installation (from Docker documentation)

Before you install Docker Engine for the first time on a new host machine, you need to set up the Docker apt repository. Afterward, you can install and update Docker from the repository.

1. Set up Docker's apt repository.

    ```
    # Add Docker's official GPG key:
    sudo apt update
    sudo apt install ca-certificates curl
    sudo install -m 0755 -d /etc/apt/keyrings
    sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
    sudo chmod a+r /etc/apt/keyrings/docker.asc

    # Add the repository to Apt sources:
    sudo tee /etc/apt/sources.list.d/docker.sources <<EOF
    Types: deb
    URIs: https://download.docker.com/linux/ubuntu
    Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
    Components: stable
    Architectures: $(dpkg --print-architecture)
    Signed-By: /etc/apt/keyrings/docker.asc
    EOF

    sudo apt update
    ```

2. Install the Docker packages.

    To install the latest version, run:

    ```sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin```

    Note

    a. After installation, verify that Docker is running:

    ```sudo systemctl status docker```

    b. If Docker is not running, start it manually:

    ```sudo systemctl start docker```

3. Verify that the installation is successful by running the hello-world image:

    ```sudo docker run hello-world```

    This command downloads a test image and runs it in a container. When the container runs, it prints a confirmation message and exits.

You have now successfully installed and started Docker Engine.


# First construction

```
sudo docker build \
    --progress=plain \
    -f docker/Dockerfile \
    -t joya-dev \
    .
```

# Comprobar la imagen

```
sudo docker image ls joya-dev
```

# Levantar la imagen

```
sudo docker run --rm -it \
  -v "ruta/a/JoyaPhoneConnectionFirmware":/workspace/app \
  -v "ruta/a/keys:/workspace/keys:ro" \
  joya-dev
```

Nota: se puede utilizar -v `"$PWD":/workspace/app` si se está parado sobre JoyaPhoneConnectionFirmware

# Compilar
```
west build -d build_joya -b joya_nrf52/nrf52832 . --sysbuild -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/workspace/keys/joya_dev-private.pem\"
```

# Armar el .hex
```
srec_cat \
  ./build_joya/mcuboot/zephyr/zephyr.hex -Intel \
  ./build_joya/app/zephyr/zephyr.signed.hex -Intel \
  -o ./files/merged.hex -Intel
```

# Grabar
1. `JLinkExe -device nRF52832_xxAA -if SWD -speed 4000`
2. `erase`
3. `loadfile files/merged.hex`
4. `r`
5. `g`
6. `q`