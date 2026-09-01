#!/usr/bin/env python3

"""Cross-platform MCUboot DFU over Bluetooth LE using smpmgr."""

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_BLE_TARGET = "nRF54LM20B SMP"
DEFAULT_IMAGE = Path(__file__).resolve().parent / (
    "build/dfu_smp_ble/zephyr/zephyr.signed.bin"
)
DEFAULT_REQUEST_TIMEOUT = 10
DEFAULT_UPLOAD_TIMEOUT = 40
DEFAULT_RECONNECT_ATTEMPTS = 12
DEFAULT_RECONNECT_DELAY = 5.0


def run_smpmgr(
    ble_target: str,
    timeout: int,
    *arguments: str,
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    command = [
        "smpmgr",
        "--timeout",
        str(timeout),
        "--ble",
        ble_target,
        *arguments,
    ]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE if capture_output else None,
        stderr=subprocess.STDOUT if capture_output else None,
        text=True,
    )
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, command)
    return result


def read_secondary_image_hash(
    ble_target: str,
    request_timeout: int,
) -> str:
    result = run_smpmgr(
        ble_target,
        request_timeout,
        "image",
        "state-read",
        capture_output=True,
    )
    output = result.stdout or ""
    print(output, end="" if output.endswith("\n") else "\n")

    for block in re.findall(
        r"ImageState\((.*?)(?=\nImageState\(|\nsplitStatus:|\Z)",
        output,
        flags=re.DOTALL,
    ):
        slot = re.search(r"\bslot=(\d+)", block)
        if slot is None or slot.group(1) != "1":
            continue

        hash_value = re.search(
            r"hash=HashBytes\(\s*'([0-9A-Fa-f\s]+)'\s*\)",
            block,
            flags=re.DOTALL,
        )
        if hash_value is None:
            break

        image_hash = re.sub(r"\s+", "", hash_value.group(1))
        if len(image_hash) in (64, 96, 128):
            return image_hash

    raise RuntimeError("could not find a valid hash for MCUboot slot 1")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Perform a safe MCUboot test update over Bluetooth LE using "
            "smpmgr. Supports Windows and Linux."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""examples:
  Linux:
    python3 dfu_update.py --ble "nRF54LM20B SMP" zephyr.signed.bin

  Windows PowerShell:
    py dfu_update.py --ble "nRF54LM20B SMP" zephyr.signed.bin

The default flow uploads, marks the secondary image for a test boot, resets,
reconnects, and confirms the running image. Use --no-confirm to keep it in
test mode so MCUboot can revert it after another reset.
""",
    )
    parser.add_argument(
        "image",
        nargs="?",
        type=Path,
        default=DEFAULT_IMAGE,
        help=f"signed MCUboot image (default: {DEFAULT_IMAGE})",
    )
    parser.add_argument(
        "--ble",
        default=DEFAULT_BLE_TARGET,
        metavar="NAME_OR_ADDRESS",
        help=f"BLE device name or address (default: {DEFAULT_BLE_TARGET})",
    )
    parser.add_argument(
        "--no-confirm",
        action="store_true",
        help="leave the updated image in MCUboot test state",
    )
    parser.add_argument(
        "--request-timeout",
        type=int,
        default=DEFAULT_REQUEST_TIMEOUT,
        metavar="SECONDS",
        help=(
            "timeout for normal SMP requests "
            f"(default: {DEFAULT_REQUEST_TIMEOUT})"
        ),
    )
    parser.add_argument(
        "--upload-timeout",
        type=int,
        default=DEFAULT_UPLOAD_TIMEOUT,
        metavar="SECONDS",
        help=(
            "timeout for each upload request "
            f"(default: {DEFAULT_UPLOAD_TIMEOUT})"
        ),
    )
    parser.add_argument(
        "--reconnect-attempts",
        type=int,
        default=DEFAULT_RECONNECT_ATTEMPTS,
        metavar="COUNT",
        help=(
            "attempts to reconnect after reset "
            f"(default: {DEFAULT_RECONNECT_ATTEMPTS})"
        ),
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=DEFAULT_RECONNECT_DELAY,
        metavar="SECONDS",
        help=(
            "delay between reconnection attempts "
            f"(default: {DEFAULT_RECONNECT_DELAY:g})"
        ),
    )
    return parser.parse_args()


def format_duration(seconds: float) -> str:
    minutes, remaining_seconds = divmod(seconds, 60)
    if minutes:
        return f"{int(minutes)}m {remaining_seconds:.1f}s"
    return f"{remaining_seconds:.1f}s"


def main() -> int:
    args = parse_args()
    image = args.image.expanduser().resolve()

    if (
        args.request_timeout <= 0
        or args.upload_timeout <= 0
        or args.reconnect_attempts <= 0
        or args.reconnect_delay < 0
    ):
        print(
            "Error: timeouts and reconnect attempts must be positive; "
            "reconnect delay cannot be negative.",
            file=sys.stderr,
        )
        return 2

    if shutil.which("smpmgr") is None:
        print(
            "Error: smpmgr is not installed or is not in PATH.\n"
            "Install it with: pipx install smpmgr",
            file=sys.stderr,
        )
        return 1

    if not image.is_file():
        print(
            f"Error: signed firmware image not found: {image}\n"
            "Build the sample first, or pass the path to zephyr.signed.bin.",
            file=sys.stderr,
        )
        return 1

    print(f"BLE target: {args.ble}")
    print(f"Firmware:   {image}\n")

    dfu_started = time.monotonic()

    def finish(return_code: int) -> int:
        elapsed = time.monotonic() - dfu_started
        print(f"\nTotal DFU time: {format_duration(elapsed)}")
        return return_code

    try:
        print("Checking the current image state...")
        run_smpmgr(
            args.ble,
            args.request_timeout,
            "image",
            "state-read",
        )

        print("\nUploading the image...")
        upload_started = time.monotonic()
        run_smpmgr(
            args.ble,
            args.upload_timeout,
            "image",
            "upload",
            "--format",
            "any",
            str(image),
        )
        print(
            "Upload time: "
            f"{format_duration(time.monotonic() - upload_started)}"
        )

        print("\nReading the uploaded image hash...")
        image_hash = read_secondary_image_hash(
            args.ble,
            args.request_timeout,
        )

        print("\nScheduling the uploaded image for a test boot...")
        run_smpmgr(
            args.ble,
            args.request_timeout,
            "image",
            "state-write",
            image_hash,
        )

        print("\nResetting into the test image...")
        run_smpmgr(
            args.ble,
            args.request_timeout,
            "os",
            "reset",
        )
    except (subprocess.CalledProcessError, RuntimeError) as error:
        if isinstance(error, subprocess.CalledProcessError):
            detail = f"smpmgr exited with status {error.returncode}"
            return_code = error.returncode
        else:
            detail = str(error)
            return_code = 1
        print(
            f"\nError: {detail}.",
            file=sys.stderr,
        )
        return finish(return_code)

    if args.no_confirm:
        print(
            "\nUpdate was left in MCUboot test mode.\n"
            "After verifying it, confirm with:\n"
            f'smpmgr --timeout {args.request_timeout} --ble "{args.ble}" '
            "image state-write --confirm"
        )
        return finish(0)

    print("\nWaiting for the updated firmware to advertise SMP...")
    for attempt in range(1, args.reconnect_attempts + 1):
        result = run_smpmgr(
            args.ble,
            args.request_timeout,
            "image",
            "state-read",
            check=False,
        )
        if result.returncode == 0:
            print(
                "\nThe updated firmware is reachable. "
                "Confirming the running image..."
            )
            try:
                run_smpmgr(
                    args.ble,
                    args.request_timeout,
                    "image",
                    "state-write",
                    "--confirm",
                )
            except subprocess.CalledProcessError as error:
                print(
                    "\nError: confirmation failed; MCUboot rollback "
                    "remains available.",
                    file=sys.stderr,
                )
                return finish(error.returncode)

            print("\nDFU update completed and the running image was confirmed.")
            return finish(0)

        if attempt < args.reconnect_attempts:
            print(
                f"Reconnect attempt {attempt}/{args.reconnect_attempts} "
                f"failed; retrying in {args.reconnect_delay:g} seconds..."
            )
            time.sleep(args.reconnect_delay)

    print(
        "\nError: the device did not become reachable after the test boot.\n"
        "The image was not confirmed; MCUboot rollback remains available.",
        file=sys.stderr,
    )
    return finish(1)


if __name__ == "__main__":
    raise SystemExit(main())
