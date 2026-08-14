# Gateway (OpenWrt) setup

Host side of the border router on the DGNWG05LM running
[openlumi](https://openlumi.github.io/) OpenWrt. The Thread stack runs in a **stock**
`otbr-agent`; you only have to point it at the JN5169 RCP and open two host-side
plumbing items (firewall + mDNS) that are easy to miss. All commands run as `root`
on the gateway.

Prerequisite: the JN5169 is already flashed with [`ot-rcp.bin`](../README.md#flashing).

## 1. Install the stock border router

openlumi's `apk` feed carries the upstream OpenThread BR:

```sh
apk update
apk add openthread-br luci-app-openthread
```

This pulls `otbr-agent`, `ot-ctl`, and the Apple **mDNSResponder / mdnsd** packages
used by the SRP advertising proxy.

## 2. Point otbr-agent at the RCP

Create `/etc/init.d/otbr-agent` (procd service). The RCP is on `ttymxc1` at
115200 8N1:

```sh
#!/bin/sh /etc/rc.common
START=90
USE_PROCD=1
start_service() {
    procd_open_instance
    procd_set_param command /usr/sbin/otbr-agent -I wpan0 -B phy0-sta0 \
        --rest-listen-address 0.0.0.0 \
        "spinel+hdlc+uart:///dev/ttymxc1?uart-baudrate=115200"
    procd_set_param respawn 3600 5 0
    procd_set_param stderr 1
    procd_close_instance
}
```

`-B phy0-sta0` is the **infrastructure** interface — the Wi-Fi/LAN the gateway is
attached to (adjust to your setup). Note: the stock `otbr-agent` (v0.3.0) does
**not** accept `--vendor-name` / `--model-name`; leave them out or it won't start.

```sh
/etc/init.d/otbr-agent enable
```

## 3. mDNS daemon for the SRP advertising proxy

otbr's SRP server hands each registration to the advertising proxy, which talks to
the mDNS daemon over the UNIX socket **`/var/run/mdnsd`**. That socket is provided
by **`mdnsd`**, *not* by `mDNSResponder` — running the wrong one gives
`connect() failed /var/run/mdnsd … Connection refused` and every SRP update fails
with `Send fail response: 5` (the device is stuck "checking Thread network").

```sh
/etc/init.d/mDNSResponder stop; /etc/init.d/mDNSResponder disable
/etc/init.d/mdnsd enable; /etc/init.d/mdnsd start
ls -l /var/run/mdnsd            # should be a socket
```

## 4. Firewall: let traffic reach the border router

`wpan0` is created at runtime by `otbr-agent` — **do not** define it as a
`/etc/config/network` interface; netifd will fight otbr for the device and disable
Thread. Instead reference the device directly from a firewall zone.

Without a zone, packets arriving on `wpan0` hit the default `REJECT` and the router
answers every ping/SRP with ICMPv6 *port-unreachable* (only loopback-sourced
traffic works). You need:

- **input ACCEPT** on `wpan0` — so Thread devices can reach the BR's own services
  (SRP server, CoAP, …);
- **forwarding both ways** between the Thread zone and the infra (`wan`) zone — so a
  Matter controller on the LAN can reach the device's OMR address and back.

```sh
# thread zone bound to the wpan0 device
Z=$(uci add firewall zone)
uci set firewall.$Z.name='thread'
uci set firewall.$Z.input='ACCEPT'
uci set firewall.$Z.output='ACCEPT'
uci set firewall.$Z.forward='ACCEPT'
uci add_list firewall.$Z.device='wpan0'

# thread <-> wan both directions (wan = the infra interface's zone)
F1=$(uci add firewall forwarding); uci set firewall.$F1.src='thread'; uci set firewall.$F1.dest='wan'
F2=$(uci add firewall forwarding); uci set firewall.$F2.src='wan';    uci set firewall.$F2.dest='thread'

uci commit firewall
/etc/init.d/firewall reload
```

`otbr-agent`'s RoutingManager already advertises the OMR prefix (a
`RIO fd..::/64`) to the infra link via Router Advertisements, so LAN hosts learn the
route automatically.

## 5. Bring it up

```sh
/etc/init.d/otbr-agent start
ot-ctl state          # -> leader (or router/child)
ot-ctl srp server state   # -> running
```

If the Thread network is new, form a dataset the usual way (`ot-ctl dataset init
new; … ; ot-ctl dataset commit active; ot-ctl ifconfig up; ot-ctl thread start`) or
let Home Assistant's Thread integration push credentials during commissioning.

## Verifying a device registered

After commissioning a Matter device you should see it as a neighbour and its
operational service in the SRP server:

```sh
ot-ctl child table            # or `neighbor table`
ot-ctl srp server host        # -> <hex-id>.default.service.arpa
ot-ctl srp server service     # -> <fabric>-<node>._matter._tcp … port 5540
```

`_matter._tcp` present == fully commissioned and operational.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `ot-ctl state` empty / no attach | RCP/host baud mismatch, or `--vendor-name` on stock agent | check the radio URL baud; drop unsupported args |
| Device pings BR → *port unreachable* | `wpan0` not in a firewall zone | section 4 |
| SRP stuck `ToAdd`, `connect() /var/run/mdnsd refused` | wrong mDNS daemon | run `mdnsd`, not `mDNSResponder` (section 3) |
| SRP client never transmits (`ToAdd`, no traffic) | `host address auto` with no service | set an explicit address + `srp client service add <inst> <svc> <port>` |
| Thread `disabled`, `wpan0` lost its addresses | `wpan0` defined as a netifd interface | remove it from `/etc/config/network`; use `device 'wpan0'` in the zone only |
| Light stops responding when spamming commands | old byte-blocking Spinel UART TX | use current `ot-rcp.bin` (non-blocking FIFO TX) |
