# bosond

This service streams frames from a FLIR Boson camera connected as a
USB video device to a
[thermal-recorder](https://github.com/TheCacophonyProject/thermal-recorder)
compatible Unix domain socket.


## Usage

```
./bosond  [-x] [-t] [-p <string>] [-d <int>] [--] [--version] [-h]


Where:

   -x,  --no-send
     Just read frames without connecting to socket

   -t,  --print-timing
     Print frame timings

   -p <string>,  --socket-path <string>
     Path to output socket

   -d <int>,  --device <int>
     Video device number to use

   --version
     Displays version information and exits.

   -h,  --help
     Displays usage information and exits.


   Read FLIR Boson frames
```

## Building

```
$ sudo apt install libtclap-dev
$ make
```

## Running

For more consistent performance run with Linux real-time FIFO priority of 1. For example:

```
chrt --fifo 1 ./bosond -t
```

If running using systemd, use the equivalent options for setting
process priority there.

On a Raspberry Pi, ensure that the `performance` CPU scaling governor
is used for a more consistent frame rate:

```
(as root)
$ echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
```

This change can be persisted across reboots by dropping an appropriate
file in `/etc/sysctl.d/`.

## References

- https://github.com/FLIR/BosonUSB
- https://en.wikipedia.org/wiki/Video4Linux
