# SMPGR functionality on Ubuntu
_Exported on 8/12/2026 at 10:57:54 GMT+2 from Cursor (3.2.11)_

---

**User**

can you tell me if the smpgr works on the ubuntu ?

---

**Cursor**

Yes—if you mean **`smpmgr`**, it supports Ubuntu/Linux.

Install with Python 3.11+:

```bash
sudo apt install pipx
pipx install smpmgr
smpmgr --help
```

It supports serial/USB, BLE, and UDP connections. For USB, you may also need Nordic udev rules or membership in the `dialout` group. [Official repository](https://github.com/intercreate/smpmgr)

---

**User**

okay can you wirte me a script to  perform  dfu update ?

@/home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble here is my app


kogr@kogr-local-ub:~$ sudo apt install pipx
pipx install smpmgr
smpmgr --help
[sudo] password for kogr: 
Reading package lists... Done
Building dependency tree... Done
Reading state information... Done
The following package was automatically installed and is no longer required:
  grub-pc-bin
Use 'sudo apt autoremove' to remove it.
The following additional packages will be installed:
  python3-pip-whl python3-platformdirs python3-psutil python3-setuptools-whl
  python3-userpath python3-venv python3.12-venv
The following NEW packages will be installed:
  pipx python3-pip-whl python3-platformdirs python3-psutil
  python3-setuptools-whl python3-userpath python3-venv python3.12-venv
0 upgraded, 8 newly installed, 0 to remove and 65 not upgraded.
Need to get 3,437 kB of archives.
After this operation, 7,495 kB of additional disk space will be used.
Do you want to continue? [Y/n] y
Get:1 http://pl.archive.ubuntu.com/ubuntu noble-updates/universe amd64 python3-pip-whl all 24.0+dfsg-1ubuntu1.3 [1,707 kB]
Get:2 http://pl.archive.ubuntu.com/ubuntu noble-updates/universe amd64 python3-setuptools-whl all 68.1.2-2ubuntu1.2 [716 kB]
Get:3 http://pl.archive.ubuntu.com/ubuntu noble-updates/universe amd64 python3.12-venv amd64 3.12.3-1ubuntu0.15 [5,674 B]
Get:4 http://pl.archive.ubuntu.com/ubuntu noble-updates/universe amd64 python3-venv amd64 3.12.3-0ubuntu2.1 [1,032 B]
Get:5 http://pl.archive.ubuntu.com/ubuntu noble/main amd64 python3-platformdirs all 4.2.0-1 [16.1 kB]
Get:6 http://pl.archive.ubuntu.com/ubuntu noble/universe amd64 python3-userpath all 1.9.1-1 [9,416 B]
Get:7 http://pl.archive.ubuntu.com/ubuntu noble/universe amd64 pipx all 1.4.3-1 [787 kB]
Get:8 http://pl.archive.ubuntu.com/ubuntu noble/main amd64 python3-psutil amd64 5.9.8-2build2 [195 kB]
Fetched 3,437 kB in 1s (5,788 kB/s)      
Selecting previously unselected package python3-pip-whl.
(Reading database ... 289013 files and directories currently installed.)
Preparing to unpack .../0-python3-pip-whl_24.0+dfsg-1ubuntu1.3_all.deb ...
Unpacking python3-pip-whl (24.0+dfsg-1ubuntu1.3) ...
Selecting previously unselected package python3-setuptools-whl.
Preparing to unpack .../1-python3-setuptools-whl_68.1.2-2ubuntu1.2_all.deb ...
Unpacking python3-setuptools-whl (68.1.2-2ubuntu1.2) ...
Selecting previously unselected package python3.12-venv.
Preparing to unpack .../2-python3.12-venv_3.12.3-1ubuntu0.15_amd64.deb ...
Unpacking python3.12-venv (3.12.3-1ubuntu0.15) ...
Selecting previously unselected package python3-venv.
Preparing to unpack .../3-python3-venv_3.12.3-0ubuntu2.1_amd64.deb ...
Unpacking python3-venv (3.12.3-0ubuntu2.1) ...
Selecting previously unselected package python3-platformdirs.
Preparing to unpack .../4-python3-platformdirs_4.2.0-1_all.deb ...
Unpacking python3-platformdirs (4.2.0-1) ...
Selecting previously unselected package python3-userpath.
Preparing to unpack .../5-python3-userpath_1.9.1-1_all.deb ...
Unpacking python3-userpath (1.9.1-1) ...
Selecting previously unselected package pipx.
Preparing to unpack .../6-pipx_1.4.3-1_all.deb ...
Unpacking pipx (1.4.3-1) ...
Selecting previously unselected package python3-psutil.
Preparing to unpack .../7-python3-psutil_5.9.8-2build2_amd64.deb ...
Unpacking python3-psutil (5.9.8-2build2) ...
Setting up python3-setuptools-whl (68.1.2-2ubuntu1.2) ...
Setting up python3-pip-whl (24.0+dfsg-1ubuntu1.3) ...
Setting up python3-platformdirs (4.2.0-1) ...
Setting up python3-psutil (5.9.8-2build2) ...
Setting up python3-userpath (1.9.1-1) ...
Setting up python3.12-venv (3.12.3-1ubuntu0.15) ...
Setting up python3-venv (3.12.3-0ubuntu2.1) ...
Setting up pipx (1.4.3-1) ...
Processing triggers for man-db (2.12.0-4build2) ...
  installed package smpmgr 0.18.1, installed using Python 3.12.3
  These apps are now globally available
    - smpmgr
done! ✨ 🌟 ✨
                                                                                
 Usage: smpmgr [OPTIONS] [COMMAND] [ARGS]...                                    
                                                                                
 Simple Management Protocol (SMP) Manager Version 0.18.1                        
                                                                                
 Copyright (c) 2023-2025 Intercreate, Inc. and Contributors                     
                                                                                
╭─ Options ────────────────────────────────────────────────────────────────────╮
│ --ip                        TEXT                    The IP address to        │
│                                                     connect to for UDP       │
│                                                     transport                │
│ --port                      TEXT                    The serial port to       │
│                                                     connect to, e.g. COM1,   │
│                                                     /dev/ttyACM0, etc.       │
│ --ble                       TEXT                    The Bluetooth address to │
│                                                     connect to               │
│ --timeout                   FLOAT                   Transport timeout in     │
│                                                     seconds; how long to     │
│                                                     wait for requests        │
│                                                     [default: 2.0]           │
│ --mtu                       INTEGER                 For UDP transport, the   │
│                                                     maximum transmission     │
│                                                     unit. Deprecated for     │
│                                                     serial transport; use    │
│                                                     --line-length and        │
│                                                     --line-buffers instead.  │
│                                                     Ignored for BLE          │
│                                                     transport.               │
│ --baudrate                  INTEGER                 The baudrate of the      │
│                                                     serial port to connect   │
│                                                     to, e.g. 115200. Will    │
│                                                     default to smpclient     │
│                                                     upstream value.          │
│ --line-length               INTEGER                 Serial transport line    │
│                                                     length in bytes. Must    │
│                                                     match the SMP server     │
│                                                     configuration. Default   │
│                                                     of None will use 128.    │
│ --line-buffers              INTEGER                 Serial transport line    │
│                                                     buffer count. Must match │
│                                                     the SMP server           │
│                                                     configuration. Default   │
│                                                     of None will use 2.      │
│ --loglevel                  [CRITICAL|ERROR|WARNIN  Debug log level          │
│                             G|INFO|DEBUG|NOTSET]                             │
│ --logfile                   PATH                    Log file path            │
│ --version                                           Show the version and     │
│                                                     exit.                    │
│ --plugin-path               PATH                    Path to plugin           │
│                                                     directory. May be used   │
│                                                     more than once.          │
│ --install-completion                                Install completion for   │
│                                                     the current shell.       │
│ --show-completion                                   Show completion for the  │
│                                                     current shell, to copy   │
│                                                     it or customize the      │
│                                                     installation.            │
│ --help                                              Show this message and    │
│                                                     exit.                    │
╰──────────────────────────────────────────────────────────────────────────────╯
╭─ Commands ───────────────────────────────────────────────────────────────────╮
│ shell        Send a shell command to the device.                             │
│ terminal     Open a terminal to the device.                                  │
│ upgrade      Upload a FW image, mark it for next boot, and reset the device. │
│ interactive  Open the `smpmgr` interactive shell. Type 'exit' or 'quit' to   │
│              exit.                                                           │
│ os           The SMP OS Management Group.                                    │
│ statistics   The SMP stat Management Group.                                  │
│ image        The SMP Image Management Group.                                 │
│ file         The SMP File Management Group.                                  │
│ enum         The SMP Enumeration Management Group.                           │
│ ic           The Intercreate User Group (64)                                 │
╰──────────────────────────────────────────────────────────────────────────────╯

kogr@kogr-local-ub:~$ 


---

**Cursor**

The sample already produces the correct artifact at `build/dfu_smp_ble/zephyr/zephyr.signed.bin`. I’ll use `smpmgr upgrade` for the safe MCUboot test swap, then reconnect and confirm only after the updated firmware exposes SMP again.

Created executable script:

`/home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble/dfu_update.sh`

Run it with:

```bash
cd /home/kogr/ncs/v3.3.0/ncs-door

---

**User**

i want it to be python script 

---

**Cursor**

Created `dfu_update.py` and removed the shell version.

Run it from the sample directory:

```bash
./dfu_update.py
```

Or specify a BLE address/image:

```bash
./dfu_update.py --ble AA:BB:CC:DD:EE:FF path/to/zephyr.signed.bin
```

Syntax and help output were validated.

---

**User**

_ble$ python3 dfu_update.py
BLE target: nRF54LM20B SMP
Firmware:   /home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble/build/dfu_smp_ble/zephyr/zephyr.signed.bin

Checking the current image state...
⠇ Connecting to nRF54LM20B SMP... OK
⠙ Waiting for image states... OK
ImageState(
    slot=0,
    version='1.0.0',
    image=None,
    hash=HashBytes(
        '3A3E37CE8C5FDD9C2417DE5A3530CDB028E3D5BE53AAC04A267ED0AEB5D1671B94D4BC0
25B3785D1BB60C430BFF282DC609F3A97776C0E702E4539B91842C8F6'
    ),
    bootable=True,
    pending=False,
    confirmed=True,
    active=True,
    permanent=False
)
ImageState(
    slot=1,
    version='1.0.1',
    image=None,
    hash=HashBytes(
        'A2A549C3C53639C9A5C2B318D0DA8298EE57EF6E02BAC81B50C1FA4EA93B7ACFC4F0922
8CF4F6B39D92E531475F7E3FDD9981338E8E4BE31FD1F840B9039500F'
    ),
    bootable=True,
    pending=False,
    confirmed=False,
    active=False,
    permanent=False
)
splitStatus: 0

Uploading the image, scheduling a test boot, and resetting...
Could not find IMAGE_TLV_SHA256 in image. If this is not an MCUboot image, retry with --format=any.

Error: smpmgr exited with status 1.
kogr@kogr-local-ub:~/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble$ 



what is goino on?

---

**Cursor**

Your firmware is valid. The issue is a `smpmgr 0.18.1` limitation:

- Your MCUboot uses **SHA-512** with Ed25519.
- `smpmgr upgrade` only searches for a **SHA-256 TLV**.
- The 128-character hash shown for slot 1 confirms SHA-512.
- The failed command did not upload anything; slot 1 already contained version `1.0.1`.

The workaround is `image upload --format any`, then manually mark slot 1’s SHA-512 hash as pending and reset. The Python script needs updating to perform that sequence instead of `smpmgr upgrade`.

---

**User**

do it 

---

**Cursor**

Updated `dfu_update.py` to support SHA-512 MCUboot images:

- Uploads with `--format any`
- Reads and parses slot 1’s hash
- Marks it for test boot
- Resets, reconnects, then confirms it

The parser was validated against your device. Run:

```bash
python3 dfu_update.py
```

---

**User**

kogr@kogr-local-ub:~/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble$ python3 dfu_update.py
BLE target: nRF54LM20B SMP
Firmware:   /home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble/build/dfu_smp_ble/zephyr/zephyr.signed.bin

Checking the current image state...
⠇ Connecting to nRF54LM20B SMP... OK
⠙ Waiting for image states... OK
ImageState(
    slot=0,
    version='1.0.0',
    image=None,
    hash=HashBytes(
        '3A3E37CE8C5FDD9C2417DE5A3530CDB028E3D5BE53AAC04A267ED0AEB5D1671B94D4BC0
25B3785D1BB60C430BFF282DC609F3A97776C0E702E4539B91842C8F6'
    ),
    bootable=True,
    pending=False,
    confirmed=True,
    active=True,
    permanent=False
)
ImageState(
    slot=1,
    version='1.0.1',
    image=None,
    hash=HashBytes(
        'A2A549C3C53639C9A5C2B318D0DA8298EE57EF6E02BAC81B50C1FA4EA93B7ACFC4F0922
8CF4F6B39D92E531475F7E3FDD9981338E8E4BE31FD1F840B9039500F'
    ),
    bootable=True,
    pending=False,
    confirmed=False,
    active=False,
    permanent=False
)
splitStatus: 0

Uploading the image, scheduling a test boot, and resetting...
Could not find IMAGE_TLV_SHA256 in image. If this is not an MCUboot image, retry with --format=any.

kogr@kogr-local-ub:~/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble$ python3 dfu_update.py
BLE target: nRF54LM20B SMP
Firmware:   /home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble/build/dfu_smp_ble/zephyr/zephyr.signed.bin

Checking the current image state...
⠴ Connecting to nRF54LM20B SMP... OK
⠙ Waiting for image states... OK
ImageState(
    slot=0,
    version='1.0.0',
    image=None,
    hash=HashBytes('3A3E37CE8C5FDD9C2417DE5A3530CDB028E3D5BE53AAC04A267ED0AEB5D1671B94D4BC025B3785D1BB60C430BFF282DC609F3A97776C0E702E4539B91842C8F6'),
    bootable=True,
    pending=False,
    confirmed=True,
    active=True,
    permanent=False
)
ImageState(
    slot=1,
    version='1.0.1',
    image=None,
    hash=HashBytes('A2A549C3C53639C9A5C2B318D0DA8298EE57EF6E02BAC81B50C1FA4EA93B7ACFC4F09228CF4F6B39D92E531475F7E3FDD9981338E8E4BE31FD1F840B9039500F'),
    bootable=True,
    pending=False,
    confirmed=False,
    active=False,
    permanent=False
)
splitStatus: 0

Uploading the image...
⠦ Connecting to nRF54LM20B SMP... OK
/home/kogr/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble/build/dfu_smp_ble/zephyr/zephyr.signed.bin ━━━━━━━━━━━━━━━━━━━━━━━━━━ 100.0% • 162.6/162.6 kB • 30.5 kB/s • 0:00:00

Reading the uploaded image hash...
⠇ Connecting to nRF54LM20B SMP... OK
⠋ Waiting for image states... OK
ImageState(
    slot=0,
    version='1.0.0',
    image=None,
    hash=HashBytes('3A3E37CE8C5FDD9C2417DE5A3530CDB028E3D5BE53AAC04A267ED0AEB5D1671B94D4BC025B3785D1BB60C430BFF282DC609F3A97776C0E702E4539B91842C8F6'),
    bootable=True,
    pending=False,
    confirmed=True,
    active=True,
    permanent=False
)
ImageState(
    slot=1,
    version='99.0.1',
    image=None,
    hash=HashBytes('C4221015A75C10954B963FE00DD9E390C60FB3E02786619DC2B8BEB8BEC7A3BB7902FFB0EB0A1EDD5948323E4CE920FAAD8B9B2957C1B84B18C6EECA496BA1B0'),
    bootable=True,
    pending=False,
    confirmed=False,
    active=False,
    permanent=False
)
splitStatus: 0

Scheduling the uploaded image for a test boot...
⠹ Connecting to nRF54LM20B SMP... OK
⠋ Waiting for image state write... OK

Resetting into the test image...
⠹ Connecting to nRF54LM20B SMP... OK
⠋ Waiting for response to ResetWrite... OK
ResetWriteResponse(
    header=Header(op=<OP.WRITE_RSP: 3>, version=<Version.V2: 1>, flags=<Flag.UNUSED: 0>, length=2, group_id=0, sequence=1, command_id=5),
    version=<Version.V2: 1>,
    sequence=1,
    smp_data=b'\x0b\x00\x00\x02\x00\x00\x01\x05\xbf\xff'
)

Waiting for the updated firmware to advertise SMP...
[10:39:10] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 1/12 failed; retrying in 5 seconds...
[10:39:25] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 2/12 failed; retrying in 5 seconds...
[10:39:41] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 3/12 failed; retrying in 5 seconds...
[10:39:56] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 4/12 failed; retrying in 5 seconds...
[10:40:11] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 5/12 failed; retrying in 5 seconds...
[10:40:27] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 6/12 failed; retrying in 5 seconds...
[10:40:42] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 7/12 failed; retrying in 5 seconds...
[10:40:57] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 8/12 failed; retrying in 5 seconds...
[10:41:13] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 9/12 failed; retrying in 5 seconds...
[10:41:28] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 10/12 failed; retrying in 5 seconds...
[10:41:43] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error
Reconnect attempt 11/12 failed; retrying in 5 seconds...
[10:41:59] ERROR    Transport error: connection timeout - common:142                                                                                                             common.py:142
⠴ Connecting to nRF54LM20B SMP... error

Error: the device did not become reachable after the test boot.
The image was not confirmed; MCUboot rollback remains available.
kogr@kogr-local-ub:~/ncs/v3.3.0/ncs-door-lock-and-access-control/samples/dfu_smp_ble$ 


*** Booting MCUboot v2.3.0-dev-fce4dac2e629 ***
*** Using nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***
I: Starting bootloader
I: Primary image: magic=good, swap_type=0x2, copy_done=0x1, image_ok=0x1
I: Secondary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
I: Boot source: none
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0x11000
I: Image version: v1.0.0
I: Jumping to the first image slot
*** Booting My Application v1.0.0-2ce38c2cf90d ***
*** Using nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***
[00:00:00.013,385] <inf> bt_sdc_hci_driver: SoftDevice Controller build revision: 
                                            27 03 7d 53 04 8d fe 99  a9 f2 9a ad de 5b 6a .
                                            74 6c ac 75                                    
[00:00:00.014,677] <inf> bt_hci_core: HW Platform: Nordic Semiconductor (0x0002)
[00:00:00.014,689] <inf> bt_hci_core: HW Variant: nRF54Lx (0x0005)
[00:00:00.014,700] <inf> bt_hci_core: Firmware: Standard Bluetooth controller (0x00) Versi7
[00:00:00.015,087] <inf> bt_hci_core: HCI transport: SDC
[00:00:00.015,134] <inf> bt_hci_core: Identity: CF:B7:FF:E3:FF:F2 (random)
[00:00:00.015,151] <inf> bt_hci_core: HCI: version 6.2 (0x10) revision 0x30a3, manufacture9
[00:00:00.015,166] <inf> bt_hci_core: LMP: version 6.2 (0x10) subver 0x30a3
[00:00:00.015,178] <inf> dfu_smp_ble: Firmware version 1.0.0
[00:00:00.015,659] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:02:25.131,113] <inf> dfu_smp_ble: SMP client connected
[00:02:26.957,648] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:02:28.847,633] <inf> dfu_smp_ble: Disconnected: 0x13 
[00:02:28.848,126] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:05:11.736,183] <inf> dfu_smp_ble: SMP client connected
[00:05:13.599,990] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:15.849,974] <inf> dfu_smp_ble: Disconnected: 0x13 
[00:05:15.850,466] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:05:29.738,150] <inf> dfu_smp_ble: SMP client connected
[00:05:30.835,341] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:32.860,326] <inf> dfu_smp_ble: Disconnected: 0x13 
[00:05:32.860,818] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:05:33.105,432] <inf> dfu_smp_ble: SMP client connected
[00:05:34.178,811] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:34.178,842] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:46.164,762] <inf> mcumgr_img_grp: Erased 0x28000 bytes of image slot
[00:05:46.438,905] <inf> mcumgr_img_grp: Erased 0x1000 bytes of image slot trailer
[00:05:53.845,255] <inf> dfu_smp_ble: Disconnected: 0x13 
[00:05:53.845,749] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:05:54.137,644] <inf> dfu_smp_ble: SMP client connected
[00:05:54.325,269] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:56.845,251] <inf> dfu_smp_ble: Disconnected: 0x13 
[00:05:56.845,744] <inf> dfu_smp_ble: Advertising as "nRF54LM20B SMP"
[00:05:57.086,257] <inf> dfu_smp_ble: SMP client connected
[00:05:57.273,159] <inf> mcuboot_util: Image index: 0, Swap type: none
[00:05:57.289,728] <inf> mcuboot_util: Image index: 0, Swap type: test
*** Booting MCUboot v2.3.0-dev-fce4dac2e629 ***
*** Using nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***
I: Starting bootloader
I: Primary image: magic=good, swap_type=0x2, copy_done=0x1, image_ok=0x1
I: Secondary image: magic=good, swap_type=0x2, copy_done=0x3, image_ok=0x3
I: Boot source: none
I: Image index: 0, Swap type: test
I: Starting swap using move algorithm.
I: Bootloader chainload address offset: 0x11000
I: Image version: v99.0.1
I: Jumping to the first image slot




wha hapeneed ?



---

**Cursor**

The DFU transport and MCUboot swap succeeded. The failure happened after MCUboot jumped to version `99.0.1`: that application never reached the Zephyr boot banner or BLE initialization, so the host could not reconnect. Because it was a test update and wasn’t confirmed, another reset should trigger MCUboot rollback.

The DFU and MCUboot swap succeeded, but the new application failed immediately after launch.

Key mismatch:

- Installed MCUboot: **NCS 3.3.0 / Zephyr 4.3.99**
- Uploaded application: **NCS 3.4.0 / Zephyr 4.4.0**
- Your `west update` upgraded the workspace despite its `v3.3.0` directory name.
- `VERSION` currently explicitly specifies `99.0.1`.

Press the board reset button once. MCUboot should detect the unconfirmed test image, revert it, and boot `1.0.0`.

Then rebuild the update with the same NCS version as the installed MCUboot, or flash the complete NCS 3.4 MCUboot/application pair over SWD before testing subsequent DFU updates. The `99.0.1` version itself is not the crash cause.

---

**User**

okay thanks, can you add the timer in the script ? so i know how much time dfu takes ?

---

**Cursor**

Added timing output to the script:

- `Upload time`: firmware transfer duration
- `Total DFU time`: full update, reboot, reconnection, and confirmation duration

It also reports total elapsed time if the DFU fails.

---

**User**

okay it works thanks!

can you write generic script for windows and linux ?
can we use smpgr on windows ?

---

**Cursor**

Yes. `smpmgr` supports Windows, Linux, and macOS. The Python implementation is already platform-independent; I’ll make its timeouts and retry behavior configurable and add cross-platform usage guidance while retaining the sample defaults.

Yes, `smpmgr` works on Windows.

I updated `dfu_update.py` as a configurable Windows/Linux script.

Windows installation and usage:

```powershell
py -m pip install pipx
py -m pipx ensurepath
pipx install smpmgr

py dfu_update.py --ble "nRF54LM20B SMP" zephyr.signed.bin
```

Linux:

```bash
python3 dfu_update.py --ble "nRF54LM20B SMP" zephyr.signed.bin
```

Run `python3 dfu_update.py --help` for timeout and retry options.

