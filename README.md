# netd

`netd` is a replacement for the FreeBSD `ifconfig(8)` command and the
`rc(8)`-based network configuration system.  It is inspired by (but different
to) Solaris's `dladm(1m)`, macOS's `networksetup` and Linux's `NetworkManager`.

`netd`'s aims are:

* To make network configuration easier and less error-prone.
* To unify "running" and "boot" configuration, so that a single command changes
  the running configuration and applies the changes to the next boot.
* To simplify common configurations like laptops that move between wired and
  wireless networks.
* To behave like a native Unix utility with a simple and intuitive command-line
  interface.
* To configure what you want it to, and not configure what you don't want it to.

`netd` is currently in a pre-alpha state and doesn't do anything useful.

## System requirements

* `netd` is tested on FreeBSD 15.0.  It may work on older versions.
    * The intention is to eventually support all supported versions of FreeBSD.

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
