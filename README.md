# ot-jn5169 — OpenThread RCP for the NXP JN5169

An OpenThread **RCP (Radio Co-Processor)** firmware for the NXP **JN5169** Zigbee
module, turning the **Xiaomi DGNWG05LM** gateway (and pin-compatible siblings such
as the Aqara ZHWG11LM) into a **Thread Border Router** for **Matter-over-Thread** —
reusing the on-board Zigbee radio instead of adding a dongle.

As far as I know this is the **first working OpenThread port to the Beyond
Architecture 2 (BA2 / `ba2`) core** that the JN516x family uses. The radio, MAC,
AES-CCM link security, 6LoWPAN fragmentation and Spinel/HDLC transport all run on
the chip; a stock `otbr-agent` on the gateway's i.MX6 speaks to it over UART.

> **Status: working end-to-end.** A real ESP32-C6 `esp-matter` light was
> commissioned over BLE, joined Thread through this border router, registered its
> operational `_matter._tcp` service via SRP, and is controllable from Home
> Assistant. See [Verification](#verification).

```
 Home Assistant / Matter controller   (anywhere on the Wi-Fi / LAN)
              │  IPv6 (OMR prefix) + mDNS
        ┌─────┴─────────────────────────────┐
        │  Xiaomi DGNWG05LM gateway          │
        │  ┌──────────────┐   UART ttymxc1   │
        │  │  i.MX6        │  Spinel/HDLC     │
        │  │  OpenWrt      │  115200 8N1      │
        │  │  otbr-agent   │◄────────────────►│  JN5169  ◄── this firmware
        │  │  (full Thread │                  │  802.15.4 radio + Spinel (RCP)
        │  │   stack, SRP) │                  │
        │  └──────────────┘                   │
        └─────────────────────────────────────┘
                    │  IEEE 802.15.4 / Thread
              Thread devices (Matter lights, sensors, …)
```

## Why RCP (and not FTD/NCP)

The JN5169 has only **32 KB of RAM**. In RCP mode the chip runs *only* the
802.15.4 radio driver and the Spinel protocol; the entire Thread stack — including
the SRP server and DNS-SD advertising proxy that Matter needs — runs on the i.MX6
inside `otbr-agent`. RCP is the only configuration that fits, and it is exactly the
one a border router wants.

## Hardware

| Part | Detail |
|------|--------|
| Gateway | Xiaomi **DGNWG05LM** (board `LM16-IGW_V1.0.3`), flashed with [openlumi](https://openlumi.github.io/) OpenWrt |
| Host SoC | NXP i.MX6 (ARM Cortex-A7), UART **`/dev/ttymxc1`** to the JN5169 |
| Radio | NXP **JN5169** (BA2 core, 32 KB RAM, 512 KB flash), on-board Zigbee module |
| Reset lines | i.MX6 `gpio40` = run/prog select, `gpio41` = reset pulse (used by `jnflash`) |

## Repository layout

```
ot-jn5169/
├── CMakeLists.txt              top-level build (RCP-only OpenThread config)
├── cmake/
│   └── ba2-toolchain.cmake     CMake toolchain for ba-elf-gcc / JN51xx flags
├── src/
│   ├── CMakeLists.txt          platform library + SDK linkage
│   ├── radio.c                 802.15.4 radio: MMAC MAC-mode RX/TX, HW auto-ACK,
│   │                           AES-CCM tx-security, 6LoWPAN frame reconstruction
│   ├── uart.c                  Spinel UART0: IRQ RX ring + non-blocking FIFO TX
│   ├── alarm.c                 microsecond/millisecond alarm (TickTimer)
│   ├── entropy.c  misc.c  system.c  startup.c  syscalls.c
│   ├── jn_macframe.cpp         802.15.4-2015 header-presence helper
│   ├── openthread-core-jn5169-config.h   OT compile-time config for this port
│   └── jn5169-rcp.ld           flattened linker script (SDK INCLUDEs inlined)
├── openthread/                 OpenThread submodule (commit 522a665, Thread 1.4)
└── third_party/nxp/JN-SW-4163/ NXP JN516x SDK — NOT redistributable, see below
```

## Prerequisites

You need three things that are **not** in this repo and must be obtained separately.

### 1. BA2 toolchain (`ba-elf-gcc` 4.7.4)

The JN516x uses the Beyond Architecture 2 core. Build the GCC 4.7.4 / binutils 2.22
cross toolchain from [**GravisZro/BA2-toolchain**](https://github.com/GravisZro/BA2-toolchain)
and install it to `~/ba2`. It builds on a modern host with a couple of legacy-C
work-arounds:

```bash
git clone https://github.com/GravisZro/BA2-toolchain
cd BA2-toolchain
# build the host-tool bits with relaxed C rules, skip texinfo docs
make CC="gcc -std=gnu89 -fcommon" MAKEINFO=true PREFIX=$HOME/ba2
export PATH="$HOME/ba2/bin:$PATH"
ba-elf-gcc --version   # -> ba-elf-gcc (GCC) 4.7.4
```

### 2. NXP JN516x SDK (JN-SW-4163)

The radio (MMAC), hardware API, boot and AES libraries are proprietary NXP blobs.
Download **JN-SW-4163** ("JN516x IEEE 802.15.4 SDK") from nxp.com (free account
required) and unpack it into `third_party/nxp/JN-SW-4163/` so that
`third_party/nxp/JN-SW-4163/Components/Library/libMMAC_JN5169.a` exists. It cannot
be redistributed here — hence the `.gitignore` entry.

### 3. CMake ≥ 3.16 and Ninja

```bash
sudo apt install cmake ninja-build
```

Tested on Ubuntu 24.04 (WSL2 works fine).

## Building

```bash
git clone --recursive https://github.com/HedgDifuse/ot-jn5169
cd ot-jn5169
export PATH="$HOME/ba2/bin:$PATH"

cmake -GNinja -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ba2-toolchain.cmake \
  -DNXP_SDK_ROOT="$PWD/third_party/nxp/JN-SW-4163" \
  -DOT_LINKER_MAP=OFF

ninja -C build ot-rcp-bin
```

The result is **`build/ot-rcp.bin`** (~61 KB), already wrapped in the JN bootloader
image format. Footprint: ~50 KB flash text, ~14.6 KB of the 32 KB RAM.

### Build gotchas (already handled, documented so you don't rediscover them)

- **newlib hides `vsnprintf` under `-std=c++11`** → the toolchain file passes
  `-U__STRICT_ANSI__`.
- **`-Wl,-Map=` crashes this old `ld`** ("invalid data statement") → keep
  `OT_LINKER_MAP=OFF`.
- **`INCLUDE` in the SDK `.ld` files doesn't resolve via `-L`** → the linker script
  is flattened into `src/jn5169-rcp.ld` (with `PROVIDE(end)`/stack symbols).
- Firmware entry is **`AppColdStart`** (`src/startup.c`); the ROM bootloader lays
  out `.data`/`.bss` itself.

## Flashing

The JN5169 hangs off the i.MX6 UART, so flash from the gateway. openlumi ships a
`jnflash` helper that drives the reset/program GPIOs and speaks the JN bootloader
protocol:

```bash
scp build/ot-rcp.bin root@<gateway>:/tmp/ot-rcp.bin
ssh root@<gateway>
  /etc/init.d/otbr-agent stop          # release the UART first
  jnflash /tmp/ot-rcp.bin              # -> "Success!"
  /etc/init.d/otbr-agent start
```

(If you don't have `jnflash`, NXP's *JN51xx Production Flash Programmer* works too;
put the chip in programming mode via the reset lines above.)

## Gateway (OpenWrt) setup

The host side uses a **stock** `otbr-agent` — no custom border-router build needed.
Full step-by-step (package install, UART radio URL, firewall zone for `wpan0`, the
mDNS daemon for the SRP advertising proxy) is in
**[`docs/gateway-setup.md`](docs/gateway-setup.md)**. The short version:

```bash
apk add openthread-br luci-app-openthread   # stock OTBR + mDNS deps
# point otbr-agent at the RCP:
#   spinel+hdlc+uart:///dev/ttymxc1?uart-baudrate=115200
# put wpan0 in a firewall zone with input ACCEPT and wan<->thread forwarding
# run the `mdnsd` service (advertising proxy backend on /var/run/mdnsd)
```

## How it works — implementation notes

The interesting bits, and the bugs that took the longest to find:

- **Full MMAC MAC-mode** RX and TX (`vMMAC_StartMacReceive` /
  `vMMAC_StartMacTransmit`) with **hardware auto-ACK** and address filtering. An
  earlier PHY-mode + software-ACK approach lost ~40 % of ACKs; MAC-mode gives
  100 % TxAcked.
- **AES-CCM tx-security on-chip.** For an RCP, `otMacFrameProcessTransmitAesCcm`
  is compiled out unless `OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE=1`
  (FTD/MTD are both 0) — without that flag every frame goes out in the clear with a
  zero MIC. Enabling it, plus a self-contained `processTxSecurity()` that always
  selects the key by the frame's key-id, fixed link security.
- **6LoWPAN fragmentation.** `reconstructPsdu()` rebuilds the on-air PSDU from the
  MMAC `tsMacFrame`. A too-conservative length guard (`payload + 25 > 127`, the
  worst-case ext+ext header) was silently dropping the *first fragment* of every
  large datagram (short-addressed frames have an ~11-byte header, so a 106-byte
  payload really fits). That single check was what blocked SRP and inter-router
  MLE. The fix validates against the *actual* header length.
- **Extended-address byte order** matches `vMMAC_GetMacAddress` (canonical split,
  `u32H` = high word); on-air bytes are little-endian.
- **Non-blocking Spinel UART TX.** The original TX sent each frame to the host
  byte-by-byte while busy-waiting on `THRE`, freezing the main loop for ~22 ms per
  frame — under a burst of Matter commands the 512-byte RX ring overflowed and HDLC
  lost sync. TX now writes up to 16 bytes into the hardware FIFO per main-loop pass
  and the RX ring is 4 KB, so the loop stays responsive.

## Verification

Measured on the bench (gateway ↔ ESP32-C6):

- gateway → device ping, multi-fragment: **6/6 at 60/100/200/400 bytes**, 0 % loss.
- rapid command burst (50 pings @ 20/s, 40 @ 25/s): **0 % loss**, RCP stays
  responsive (this is the "spam on/off in HA" case).
- SRP registration of a device `_matter._tcp` service: **Registered in ~4 s**.
- Full Matter commissioning of an `esp-matter` light through Home Assistant:
  **success**, light controllable.

Known limit: a pathological 400-byte-fragment flood at 10/s still drops packets —
that is a half-duplex radio ceiling, not a UART or stack issue, and does not affect
normal Matter traffic. UART is kept at 115200; 500000 is achievable on the JN5169
(divisor 2) but is unreliable on this board under load without RTS/CTS, which the
i.MX6 `ttymxc1` device-tree does not wire.

## Credits & license

- Built on [OpenThread](https://github.com/openthread/openthread) (BSD-3-Clause).
- [openlumi](https://openlumi.github.io/) — OpenWrt for the DGNWG05LM.
- [GravisZro/BA2-toolchain](https://github.com/GravisZro/BA2-toolchain).
- NXP JN-SW-4163 SDK is © NXP and is **not** included; obtain it from NXP.

The original work in this repository (`src/`, the CMake glue and docs) is released
under the **PolyForm Noncommercial License 1.0.0** — free to use, modify and
redistribute for any **noncommercial** purpose; commercial use needs a separate
license from the copyright holder. See [`LICENSE`](LICENSE). OpenThread stays
BSD-3-Clause and the NXP SDK / BA2 toolchain keep their own licenses.
