# F-01: bounded worker-pool experiment

## Question and designs

Does processing independent requests with a fixed pthread worker pool reduce
priority-weighted latency while preserving correctness?

- Serial baseline: commit `b9362c4717e2192f6bfe2fd9941476a097d1b92a`.
- Candidate: `experiment-worker-pool`, a bounded FIFO of 1,024 accepted sockets,
  mutex/condition-variable synchronization, and configurable fixed workers.
- Hash algorithm, packet format and per-request search are unchanged.
- Compare 1, 2 and 4 candidate workers. The default is the online CPU count,
  capped at 64; use `THREADRUNNER_WORKERS` to control the experiment.

The queue lock covers only enqueue/dequeue. Workers perform blocking socket
I/O and hashing after releasing it. A stalled connection occupies one worker;
this is not protection against enough stalled connections to occupy the pool.

## Build and test

Run in Linux, from the repository root:

```bash
make clean
make CFLAGS='-std=gnu11 -Wall -Wextra -Werror -O2'
THREADRUNNER_WORKERS=4 python3 tests/run_local_checks.py
python3 tests/test_worker_pool.py
```

The concurrency script captures the same check previously run inline: 64
known-answer requests from 12 client threads, while a first connection sends
13 bytes and remains incomplete. Correctness uses independent Python hashlib.
The saved evidence below comes from that original inline run.

## Cross-VM benchmark

On the server VM:

```bash
THREADRUNNER_WORKERS=4 ./server 5003 2>&1 | tee worker-pool-4-final-server.log
```

On the client VM, create a new results directory per run:

```bash
run_dir=$(mktemp -d /home/vagrant/threadrunners-pool4-results.XXXXXX)
set -o pipefail
/home/vagrant/os-challenge-common/x86_64/bin/linux/client 192.168.101.10 5003 5041 500 0 30000000 20 600000 1.5 2>&1 | tee "$run_dir/final-client.log"
run_status=("${PIPESTATUS[@]}")
printf 'client_exit=%s\ntee_exit=%s\n' "${run_status[0]}" "${run_status[1]}" > "$run_dir/exit-status.txt"
```

Settings: seed 5041; 500 requests; random start; difficulty 30,000,000;
repetition 20%; delay 600,000 microseconds; priority lambda 1.5. The assessment
seed is not known. Preserve client arguments, VM resources, compiler flags and
logging conditions across comparisons. Run one benchmark at a time. Stabilize
VM clocks before timing; do not change clock or VM settings during a run.

Parse every response row: require exactly 500 rows, flag 1, expected number
equal to received answer, and successful client/logging exit statuses. Compute
the score as sum(latency_us * priority) / request_count. Preserve raw logs,
source checksums and worker count. Repeat comparisons and test additional
seeds before drawing a final performance conclusion.

## Correctness checks

`results/` contains original completed logs and summaries copied from the VMs.
The candidate source SHA256 is
`e417ea2ed3ce1cc764f046ae77b9862290c3550d63ec78ea8abe7f98802988ab`.

| Check | Result |
| --- | --- |
| Strict Linux build, GCC 13.3.0 | PASS |
| Invalid command-line cases | 6 passed |
| Protocol/boundary/recovery tests | 12 passed |
| Official small client, loopback | 20/20 correct |
| Concurrent-client check, 4 workers | 64/64 correct; incomplete connection stayed open |

## Benchmark results

The serial baseline and all three worker-pool runs used seed 5041 and the
same 500-request workload. The raw logs contain the same set of request
numbers and priorities, including repeats. All client and log exit codes
were 0. Scores were recalculated from the raw client logs.

| Server | Correct | Weighted latency score (us) | Versus serial | Raw log and saved evidence |
| --- | ---: | ---: | ---: | --- |
| Original serial server | 500/500 | 3,154,329,362.050 | Baseline | [Evidence](results/serial-seed5041-client.log) |
| One worker | 500/500 | 3,004,263,279.312 | 4.8% lower | [Evidence](results/pool1-seed5041/) |
| Two workers | 500/500 | 1,450,348,990.396 | 54.0% lower | [Evidence](results/pool2-seed5041/) |
| Four workers | 500/500 | 452,930,228.852 | 85.6% lower | [Evidence](results/pool4-seed5041/) |

Four workers had the lowest score on this workload: 85.6% lower than
the serial baseline. The score averages priority times latency across
requests, including queueing; it is not total benchmark duration.

The four-worker evidence includes checksums of the tested source and
Makefile. The one- and two-worker evidence includes the raw client logs,
exit codes and summaries; separate server-side checksums were not
captured for those runs.

The candidate build and local summaries identify Linux 6.8.0-139-generic and
GCC 13.3.0. The server VM reports four logical CPUs; the observed candidate
process has four workers plus its main thread. Environment details are saved
in `results/candidate-server-environment.json`.

## Remaining work

- Repeat measurements and use additional seeds to check how stable the result is.
- Choose the final configuration after checking the intended evaluation environment.
- Update the course report with motivation, setup, results and conclusion.

This comparison uses one run per configuration with seed 5041. Four workers
are the leading choice on the tested VM; the final worker count is still open.
Priority scheduling and caching remain separate experiments.
