Local dependency patches
========================

The STM32N6570-DK workspace uses local changes to upstream Zephyr and MCUboot.
Keep these patches with the SDK so a fresh workspace can reproduce the build.

Current baseline:

* Zephyr: bbc6385f0a2c (v4.5.0-rc1 development tree).
* MCUboot: a8ffd2c312910cc648219bcb29e04260f7857492.

From the west workspace root, apply to clean dependency checkouts::

    git -C zephyr apply --check ../sdk/patch/zephyr-202606140732.patch
    git -C zephyr apply ../sdk/patch/zephyr-202606140732.patch
    git -C zephyr apply --check ../sdk/patch/zephyr-stm32-xspim-non-xip.patch
    git -C zephyr apply ../sdk/patch/zephyr-stm32-xspim-non-xip.patch
    git -C bootloader/mcuboot apply --check ../../sdk/patch/mcuboot-stm32n6-backup-domain.patch
    git -C bootloader/mcuboot apply ../../sdk/patch/mcuboot-stm32n6-backup-domain.patch

The June patch retains the Ethernet 10/100 Mbit port selection and optional
AXISRAM backup-domain access changes, using the current AXISRAM source path.
The XSPI patch permits manager initialization for images loaded into RAM while
preserving the restriction on executing directly from external flash.
The MCUboot patch enables STM32 backup-domain access before boot initialization
and is specific to this STM32 build.

If a patch is already present, ``git apply --reverse --check`` succeeds; do not
apply it twice. Recheck patches after updating dependency revisions. The older
``zephyr-202601031453.patch`` is historical and is not part of this baseline.
