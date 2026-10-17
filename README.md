# ThreadRunners OS Challenge

A serial Linux/POSIX baseline for DTU 02159. One TCP connection carries one
49-byte reverse-SHA256 request and one 8-byte answer. The search range is
[start, end). Hash inputs are little-endian uint64 values; transmitted bounds
and answers are big-endian.

## Build and run

Requires GCC and Make. Python 3 is used for tests. No OpenSSL development
headers or additional hashing libraries are needed.

~~~bash
make clean
make
./server 5003
~~~

The server binds to all IPv4 interfaces. The port must be from 1 to 65535.

## Files

- server.c: connection handling, complete I/O, request parsing and serial search.
- messages.h: packet sizes, offsets and endian helpers.
- lonesha256.h: the supplied SHA256 implementation, preserved unchanged.
- Makefile: builds the server at the repository root.
- tests/test_protocol.py: independent protocol, boundary and recovery tests.
- tests/run_local_checks.py: Linux build checks and the official small client.
- test-results/: generated evidence, excluded from Git.

## Tests

With a server already running in the server VM:

~~~bash
python3 tests/test_protocol.py --host 192.168.101.10 --port 5003
~~~

To build and run automated loopback checks on a Linux VM that has the common
repository at /home/vagrant/os-challenge-common:

~~~bash
python3 tests/run_local_checks.py
~~~

The automation starts its own temporary server and stops it afterward. It uses
the official x86_64 Linux client and the small test's actual seed, total, range,
repetition, delay and priority settings. Only host and port are changed to
loopback. It saves compiler, protocol, client and server logs in test-results/.

For the real two-VM milestone test, run this in the client VM while the server
is listening on port 5003:

~~~bash
cd /home/vagrant/os-challenge-common
./run-client.sh
./run-client-milestone.sh
~~~

The full cross-VM milestone test remains a separate release requirement.
A loopback small-client pass does not establish full milestone reliability.

## Branches and release

milestone-basic-server contains the developing baseline. Integrate the tested
milestone release into the default branch before hand-in and identify the
exact tested commit in ThreadRunners-milestone.csv. Experimental performance
changes belong on separate branches or in separate directories.

The baseline parses priority but processes connections serially. Caching,
parallel workers and priority scheduling are future experiments. Connections
that send no complete request and remain open can still block this baseline;
the recovery tests cover clients that close or half-close incomplete input.
