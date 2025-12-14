# Zephyr sdk for Application

This repository is forked from a Zephyr example application.
The main purpose of this repository is to serve as a reference on how to structure Zephyr-based
applications. Some of the features demonstrated in this example are:

- Basic [Zephyr application][app_dev] skeleton
- [Zephyr workspace applications][workspace_app]
- [Zephyr modules][modules]
- [West T2 topology][west_t2]
- [Custom boards][board_porting]
- Custom [devicetree bindings][bindings]
- Out-of-tree [drivers][drivers]
- Out-of-tree libraries
- Example CI configuration (using GitHub Actions)
- Custom [west extension][west_ext]
- Custom [Zephyr runner][runner_ext]
- Doxygen and Sphinx documentation boilerplate

[app_dev]: https://docs.zephyrproject.org/latest/develop/application/index.html
[workspace_app]: https://docs.zephyrproject.org/latest/develop/application/index.html#zephyr-workspace-app
[modules]: https://docs.zephyrproject.org/latest/develop/modules.html
[west_t2]: https://docs.zephyrproject.org/latest/develop/west/workspaces.html#west-t2
[board_porting]: https://docs.zephyrproject.org/latest/guides/porting/board_porting.html
[bindings]: https://docs.zephyrproject.org/latest/guides/dts/bindings.html
[drivers]: https://docs.zephyrproject.org/latest/reference/drivers/index.html
[zephyr]: https://github.com/zephyrproject-rtos/zephyr
[west_ext]: https://docs.zephyrproject.org/latest/develop/west/extensions.html
[runner_ext]: https://docs.zephyrproject.org/latest/develop/modules.html#external-runners

## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

### Initialization

The first step is to initialize the workspace folder (``sdkz``) where
the ``sdk`` and all Zephyr modules will be cloned. Run the following
command:

```shell
cd ~
# Create a new virtual environment and activate: -linux/MacOS
python3 -m venv sdkz/.venv && . sdkz/.venv/bin/activate
# Create a new virtual environment and activate: -Windows
python -m venv sdkz/.venv && ./sdkz/.venv/Scripts/activate

# initialize sdkz for the sdk for zephyr application (main branch)
python -m pip install --upgrade pip && python -m pip install west

west init -m https://github.com/doyoonk/sdkz.git --mr main sdkz

cd sdkz && west update && west zephyr-export

# update Zephyr modules
cd zephyr && west packages pip --install && west sdk install && cd ..
```

### Building and running

To build the application, run the following command:

```shell
cd ~/sdkz
west build -b $BOARD sdk/app -- '-DCONFIG_BOOTLOADER_MCUBOOT=y -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="etc/ssl/certs/mcuboot_sign-rsa-2048.pem"'
```

where `$BOARD` is the target board.

You can use the `custom_plank` board found in this
repository. Note that Zephyr sample boards may be used if an
appropriate overlay is provided (see `app/boards`).

A sample debug configuration is also provided. To apply it, run the following
command:

```shell
west build -b $BOARD sdk/app -- '-DEXTRA_CONF_FILE=debug.conf -DCONFIG_BOOTLOADER_MCUBOOT=y -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="etc/ssl/certs/mcuboot_sign-rsa-2048.pem"'
```

Once you have built the application, run the following command to flash it:

```shell
west flash
```

### Testing

To execute Twister integration tests, run the following command:

```shell
west twister -T tests --integration
```

### MCUboot Building for jz_f407ve_dvk

To build the MCUboot, run the following command:

```shell
cd sdkz
west build -b jz_f407ve_dvk//mcuboot bootloader/mcuboot/boot/zephyr -d build/jz_f407ve_dvk/mcuboot
```

### Documentation

A minimal documentation setup is provided for Doxygen and Sphinx. To build the
documentation first change to the ``doc`` folder:

```shell
cd doc
```

Before continuing, check if you have Doxygen installed. It is recommended to
use the same Doxygen version used in [CI](.github/workflows/docs.yml). To
install Sphinx, make sure you have a Python installation in place and run:

```shell
pip install -r requirements.txt
```

API documentation (Doxygen) can be built using the following command:

```shell
doxygen
```

The output will be stored in the ``_build_doxygen`` folder. Similarly, the
Sphinx documentation (HTML) can be built using the following command:

```shell
make html
```

The output will be stored in the ``_build_sphinx`` folder. You may check for
other output formats other than HTML by running ``make help``.
