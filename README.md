# ctr-uefi / CtrPkg

> [!NOTE]
> This project is using LLMs.

## Overview
ctr-uefi / CtrPkg is an experimental implementation of UEFI firmware for the Nintendo 3DS family with Luma3DS (tested with v13.5).

## Usage
1. Deploy built `ctr-uefi.firm` to `/luma/payloads`.
2. Hold START button to open the menu to select firmware to boot.
3. In the menu, select `ctr-uefi`.  
(Optional) To run EFI sample application, copy the binary to `/EFI/BOOT/BOOTARM.EFI`.

## Third-party notice
See `NOTICE` for all third-party libraries, and their license conditions.

## License
This project is available under GPL-2.0 or later. See `LICENSE` for full license text of GPL-2.0 License.
