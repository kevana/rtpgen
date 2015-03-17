#rtpgen

This tool sends a pseudo-MPEG-1 RTP stream to the selected address. Most header bits are zeroed, only Sequence number and timestamp are updated.

##History
This was created for a class project to measure the affects of different traffic types on overall network impact in a DDoS environment. The user can choose both the rate at which packets are sent, as well as the size and payload of each packet.

##Compilation

The makefile has targets for linux, OS X, and Windows. Linux is the default target.

    $ make          (linux)
    $ make osx      (OS X)
    $ make win32    (Windows)

##Usage
rtpgen run with no arguments will default to address `127.0.0.1`, port `9000`, and a rate of `1` packet per second.

    $ ./rtpgen

To see a list of available configuration options, run rtpgen with the `-h` or `--help` flags

    $ ./rtpgen -h
    $ ./rtpgen --help
