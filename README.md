# netd

netd is a replacement for the FreeBSD `ifconfig(8)` command and the
`rc(8)`-based network configuration system.  It is inspired by (but different
to) Solaris's `dladm(1m)`, macOS's `networksetup` and Linux's `NetworkManager`.

netd's aims are:

* To make network configuration easier and less error-prone.
* To unify "running" and "boot" configuration, so that a single command changes
  the running configuration and applies the changes to the next boot.
* To simplify common configurations like laptops that move between wired and
  wireless networks.
* To behave like a native Unix utility with a simple and intuitive command-line
  interface.
* To configure what you want it to, and not configure what you don't want it to.

netd is currently in a pre-alpha state and doesn't do anything useful.

## System requirements

* FreeBSD 15.0.  It may work on older versions.
    * The intention is to eventually support all supported versions of FreeBSD.
* `NETLINK` support in the kernel (this is included in `GENERIC`).
* If running in a jail, only VNET jails are supported.  (Running netd in a
  non-VNET jail probably doesn't make sense anyway.)

## Build

```
# make depend all install
```

## Run

Start `netd`.

## Example

```
# netctl list-interfaces
NAME
wg0
tap0
bridge0
alc0
lo0
ix1
ix0
```

## Programmatic output

`netctl` supports parseable output in various formats using the `libxo(3)`
library:

```
# netctl --libxo=json list-interfaces
{"interface-list": {"interface": [{"name":"wg0"}, {"name":"tap0"}, {"name":"bridge0"}, {"name":"alc0"}, {"name":"lo0"}, {"name":"ix1"}, {"name":"ix0"}]}}
# netctl --libxo=xml list-interfaces
<interface-list><interface><name>wg0</name></interface><interface><name>tap0</name></interface><interface><name>bridge0</name></interface><interface><name>alc0</name></interface><interface><name>lo0</name></interface><interface><name>ix1</name></interface><interface><name>ix0</name></interface></interface-list>
```

## Development

To build with `cc -Weverything`:

```
% make WEVERYTHING=yes
```

Note that this disables a few warnings usually enabled by `-Weverything`.

To run `clang-analyser`:

```
% make lint
```
