# ThreadRunners OS Challenge

Linux/POSIX reverse-SHA256 server for DTU 02159. One TCP connection carries one
49-byte request and one 8-byte answer. The search range is [start, end). Hash
inputs are little-endian uint64 values; transmitted bounds and answers are
big-endian. The supplied lonesha256 implementation is preserved unchanged.

## Build and run

Requires GCC, Make and POSIX threads. Python 3 is used for tests. No OpenSSL
development headers or additional hashing libraries are required.

```bash
make clean
make
./server 5003
```

The server binds to all IPv4 interfaces. The port must be from 1 to 65535.

On `experiment-worker-pool`, the accept thread enqueues client sockets in a
bounded FIFO queue of 1,024 entries. Fixed pthread workers receive requests,
search, reply and close their sockets. Queue access is protected by a mutex
and condition variables; hashing and socket I/O happen outside the queue lock.

The default worker count is the online CPU count, capped at 64. Set
`THREADRUNNER_WORKERS` to select 1 through 64 workers for an experiment:

```bash
THREADRUNNER_WORKERS=4 ./server 5003
```

The command-line interface remains `./server PORT`. Run one server at a time
on a given port. The examples above are Linux/Bash commands.

## Files

- `server.c`: complete socket I/O, protocol parsing, reverse search and worker pool.
- `messages.h`: packet sizes, offsets and endian helpers.
- `lonesha256.h`: supplied hashing implementation, unchanged.
- `Makefile`: builds the root-level `server` executable with `-pthread`.
- `tests/test_protocol.py`: independent protocol, boundary and recovery tests.
- `tests/run_local_checks.py`: strict Linux build and official small-client checks.
- `tests/test_worker_pool.py`: concurrent-client and incomplete-connection check.
- `experiments/worker-pool/`: experiment instructions and completed evidence.
- `test-results/`: generated test output, excluded from Git.

## Tests

With a server already running in the server VM:

```bash
python3 tests/test_protocol.py --host 192.168.101.10 --port 5003
```

To build and run loopback checks on a Linux VM containing the common
repository at `/home/vagrant/os-challenge-common`:

```bash
THREADRUNNER_WORKERS=4 python3 tests/run_local_checks.py
python3 tests/test_worker_pool.py
```

The local checker starts and stops its own temporary server. It preserves the
small client's seed, total, range, repetition, delay and priority settings,
overriding only host and port. It saves build, protocol and client logs under
`test-results/`. The worker-pool test starts its own four-worker server and
checks 64 requests using 12 client threads while one incomplete connection
stays open. It saves a separate evidence directory.

The four-worker cross-VM run returned 500/500 correct answers for seed 5041.
Its priority-weighted latency score was 85.6% lower than the serial baseline
in that run. See `experiments/worker-pool/README.md` for the evidence.

## Branches and release

`main` and `milestone-basic-server` retain milestone release
`b9362c4717e2192f6bfe2fd9941476a097d1b92a`, the serial baseline.
`experiment-worker-pool` contains the bounded-worker experiment. Its protocol
and hashing semantics are unchanged from the milestone baseline.

Experiment details, fixed client arguments, saved baseline results and pending
comparisons are documented in `experiments/worker-pool/README.md`.

Priority is parsed but does not determine scheduling. Caching is not implemented.
An incomplete connection occupies one worker; enough stalled clients can occupy
every worker. This version does not implement per-client receive timeouts.

Final integration and submission require a tested release on the default
branch, teacher access, the final CSV and the course report. This experiment
checkpoint is not a final submission.
