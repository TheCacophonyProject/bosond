# bosond

This service streams frames from a FLIR Boson camera connected as a
USB video device to a
[thermal-recorder](https://github.com/TheCacophonyProject/thermal-recorder)
compatible Unix domain socket.


## Usage

```
   ./bosond  [-t <bool>] [-p <string>] [-d <int>] [--] [--version] [-h]

Where:

   -t <bool>,  -- <bool>
     Print frame timings

   -p <string>,  --socket-path <string>
     Path to output socket

   -d <int>,  --device <int>
     Video device number to use

   --,  --ignore_rest
     Ignores the rest of the labeled arguments following this flag.

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

## References

- https://github.com/FLIR/BosonUSB
- https://en.wikipedia.org/wiki/Video4Linux
