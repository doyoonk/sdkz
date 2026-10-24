Local dependency patches
========================

The STM32N6570-DK workspace uses local changes to upstream Zephyr.
Keep these patches with the SDK so a fresh workspace can reproduce the build.

Current baseline:

* Zephyr: 2a9e8ac155a3 (v4.5.0-rc3 development tree).
* MCUboot: 725d63c9 (v2.5.0).

From the west workspace root, apply to clean dependency checkouts::

    git -C zephyr apply --check ../sdk/patch/zephyr-202606140732.patch
    git -C zephyr apply ../sdk/patch/zephyr-202606140732.patch
    git -C zephyr apply --check ../sdk/patch/zephyr-stm32-xspim-non-xip.patch
    git -C zephyr apply ../sdk/patch/zephyr-stm32-xspim-non-xip.patch

The June patch retains only the Ethernet 10/100 Mbit port selection change.
The XSPI patch permits manager initialization for images loaded into RAM while
preserving the restriction on executing directly from external flash.

No MCUboot patch is required for backup SRAM at this baseline. Zephyr's
``stm32_backup_sram`` driver initializes it at ``PRE_KERNEL_1`` and holds one
permanent backup-domain access reference. Both images enable this driver.
The SDK DTS reserves the final 1 KiB at ``0x3c001c00`` as ``mmio-sram`` for
retained memory; only the actual hardware node uses ``st,stm32-backup-sram``.

For workspaces with the previous patches applied, remove the added
``stm32_backup_domain_enable_access()`` declaration and call from MCUboot's
``boot/zephyr/main.c`` and the ``hu_stm32_bkdomain`` conditional block from
Zephyr's ``soc/st/stm32/stm32n6x/axisram/stm32n6_axisram.c``. Preserve other
local changes. The obsolete MCUboot patch and AXISRAM hunk have been removed
from this patch set.

If a patch is already present, ``git apply --reverse --check`` succeeds; do not
apply it twice. Recheck patches after updating dependency revisions. The older
``zephyr-202601031453.patch`` is historical and is not part of this baseline;
do not apply its backup-domain reference-count workaround to this baseline.

STM32N6 contiguous system RAM
----------------------------

No additional Zephyr patch is required for CACHEAXI RAM and VENCRAM system
SRAM. The SDK supplies the ``st,stm32n6-system-sram`` devicetree binding,
``CONFIG_STM32N6_SYSTEM_SRAM`` in ``drivers/memc/Kconfig``, and initialization
in the existing ``drivers/memc/stm32n6_extra_sram.c`` driver.

The board's common overlay enables ``st,stm32n6-system-sram``. MCUboot defaults
to ``CONFIG_STM32N6_SYSTEM_SRAM=y``. The initializer preserves RAM contents,
requires the NPU, NPU cache and VENC to remain disabled, and runs before
MCUboot copies or starts the application. The application does not repeat
this initialization by default; it can explicitly enable the same option.
AXISRAM3-6 continue to use Zephyr's existing RAMCFG initialization through
their enabled DTS nodes.

The common SDK board overlay extends the upstream ``axisram12`` address
translation range and ``axisram1`` to 4224 KiB at ``0x34000000``, including
CACHEAXI RAM and VENCRAM. It retains ``axisram2`` at ``0x34180400`` with
511 KiB. The inherited board settings select ``axisram1`` as the application's
``zephyr,sram`` and as ``mcuboot,image-ram``. Both FSBL and SB inherit
``axisram2`` as ``zephyr,sram``. The duplicate SDK ``axisrama`` and
``axisramb`` nodes have been removed.

VENCRAM is not also registered with the shared heap. The unused end of
primary RAM is available to libc malloc. Although the image RAM window
covers 4224 KiB, image loading must not overwrite the running bootloader.
Program the updated MCUboot before using the expanded application RAM.
A direct debugger launch must provide equivalent memory initialization
before starting the application.

The upstream STM32N6 ``ram_check.ld`` derives its boundary from
``axisram2``. Both images now use the same secure address alias, but the
assertion checks ``__kernel_ram_start``, not the entire RAM-loaded image.
Check the complete image, including its header and TLVs, against
``0x34180400`` when changing image size or load address. The application's
unused primary SRAM becomes available to libc malloc after MCUboot hands
over control; the bootloader's RAM is not a second independent memory bank.
