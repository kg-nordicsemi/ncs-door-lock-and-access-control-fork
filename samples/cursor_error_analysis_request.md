# Error analysis request
_Exported on 8/12/2026 at 10:52:12 GMT+2 from Cursor (3.2.11)_

---

**User**

@/home/kogr/.cursor/projects/home-kogr-aliro-door-lock-syrs/terminals/4.txt:287-381 

what is this error ?

---

**Cursor**

The build targets the wrong SoC variant.

This sample supports only:

```bash
nrf54lm20dk/nrf54lm20b/cpuapp
```

You selected `nrf54lm20a/cpuapp`. The M20A configuration does not enable a flash driver, but MCUboot requires `FLASH_MAP`, producing:

> `FLASH_MAP ... dependencies FLASH_HAS_DRIVER_ENABLED with value n`

Build with:

```bash
west build -b nrf54lm20dk/nrf54lm20b/cpuapp -p
```

The `NRF_PLATFORM_LUMOS` deprecation is another warning, but the flash/MCUboot incompatibility confirms this sample is not configured for M20A.

---

**User**

can i build this app for nrf54lm20a ?

---

**Cursor**

Not directly. This DFU sample is officially configured only for `nrf54lm20b`.

You could port it to the M20A target by adding an M20A-specific MCUboot configuration that enables `CONFIG_FLASH=y` and handles the deprecation warning, but that path is unsupported and needs validation of partitions, MCUboot, DFU, and flashing.

Also, the current DK physically contains an M20B; the M20A board target is a compatibility/development configuration on that DK. Prefer:

```bash
west build -b nrf54lm20dk/nrf54lm20b/cpuapp -p
```

unless you specifically need an M20A-compatible binary.

---

**User**

@/home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble 

modyfi this app so i can build it for lm20b and lm20a

---

**Cursor**

The source changes are in place. The verification shell lacked the nRF Connect toolchain environment, so I’m rerunning both builds with the same bundled toolchain used in your terminal.

Modified the sample to support both LM20A and LM20B:

- Added LM20A-specific MCUboot configuration.
- Added LM20A to `sample.yaml`.
- Changed the BLE name to generic `nRF54LM20 SMP`.
- Successfully built both targets.

Build commands:

```bash
west build -b nrf54lm20dk/nrf54lm20a/cpuapp -p
west build -b nrf54lm20dk/nrf54lm20b/cpuapp -p
```

---

**User**

the application doesnt'work the lm20a and lm20b dont boot after flashing the fimrware

---

**Cursor**

I found the boot failure: MCUboot and the signed application are both linked at address `0x10000`. Flashing the application overwrites MCUboot, so neither board can boot. The build succeeded, but the image layout is invalid.

---

**User**

@nrf54lm20dk_nrf54lm20a_cpuapp.overlay (1-6) 

what does it do ?

---

**Cursor**

This overlay tells the linker to place the MCUboot executable in `boot_partition` at address `0x0`.

Without it, MCUboot inherited `slot0_partition` at `0x10000`, the same address as the application. Flashing the application therefore overwrote MCUboot.

Correct layout:

- MCUboot: `boot_partition` at `0x0`
- Application: `slot0_partition` at `0x10000`

Because the overlay is under `sysbuild/mcuboot/`, it only affects MCUboot.

