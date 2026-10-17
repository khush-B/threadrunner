"""Build and check the server in Linux; official small-client settings are preserved.

This uses loopback. It does not replace the cross-VM milestone workload test.
"""

import hashlib
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path


def main():
    repo = Path(__file__).resolve().parents[1]
    evidence = repo / 'test-results'
    evidence.mkdir(exist_ok=True)
    common = Path('/home/vagrant/os-challenge-common')

    def run(command, log_name, timeout=30):
        result = subprocess.run(command, cwd=repo, capture_output=True,
                                text=True, timeout=timeout)
        (evidence / log_name).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{command[0]} failed; inspect {log_name}')
        return result.stdout + result.stderr

    run(['make', 'clean'], 'clean.log')
    run(['make', 'CFLAGS=-std=gnu11 -Wall -Wextra -Werror -O2'], 'build.log')
    dependencies = run(['gcc', '-M', 'server.c'], 'headers.log')
    assert 'openssl/' not in dependencies, 'OpenSSL headers are still required'

    invalid_arguments = [[], ['abc'], ['0'], ['65536'], ['-1'], ['5003', 'extra']]
    for arguments in invalid_arguments:
        result = subprocess.run([str(repo / 'server'), *arguments], cwd=repo,
                                capture_output=True, text=True, timeout=3)
        assert result.returncode != 0, f'Invalid arguments accepted: {arguments}'

    with socket.socket() as chooser:
        chooser.bind(('127.0.0.1', 0))
        port = chooser.getsockname()[1]

    config = {}
    for line in (common / 'run-client.sh').read_text().splitlines():
        if '=' in line and not line.startswith('#'):
            key, value = line.split('=', 1)
            config[key] = value.strip()
    keys = ['SEED', 'TOTAL', 'START', 'DIFFICULTY', 'REP_PROB_PERCENT',
            'DELAY_US', 'PRIO_LAMBDA']
    values = [config[key] for key in keys]

    with (evidence / 'server.log').open('w') as server_log:
        server = subprocess.Popen([str(repo / 'server'), str(port)], cwd=repo,
                                  stdout=server_log, stderr=subprocess.STDOUT)
        try:
            for _ in range(50):
                if server.poll() is not None:
                    raise RuntimeError('Server exited during startup')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError('Server did not become ready')

            protocol_output = run([sys.executable, 'tests/test_protocol.py',
                                   '--host', '127.0.0.1', '--port', str(port)],
                                  'protocol.log')
            assert 'Ran 12 tests' in protocol_output and 'OK' in protocol_output

            command = [str(common / 'x86_64/bin/linux/client'), '127.0.0.1',
                       str(port), *values]
            official_output = run(command, 'official-small.log', timeout=60)
            answers = re.findall(r'^\[([01])\s+(\d+)\s+(\d+)\s+(\d+)\]\s+(\d+)',
                                 official_output, flags=re.MULTILINE)
            expected = int(config['TOTAL'])
            assert len(answers) == expected, 'Official response count differs from TOTAL'
            assert all(row[0] == '1' and row[1] == row[2] for row in answers)
            assert server.poll() is None, 'Server exited during the checks'
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

    summary = {
        'platform': subprocess.check_output(['uname', '-sr'], text=True).strip(),
        'compiler': subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0],
        'source_sha256': hashlib.sha256((repo / 'server.c').read_bytes()).hexdigest(),
        'warnings_as_errors_build': 'PASS',
        'openssl_headers_required': False,
        'invalid_argument_checks': len(invalid_arguments),
        'protocol_tests': 12,
        'protocol_result': 'PASS',
        'official_small_correct': len(answers),
        'official_small_total': expected,
        'official_small_reliability_percent': 100,
        'official_small_config': {key: config[key] for key in keys},
        'network': 'loopback on one Linux VM; host and port overridden',
        'cross_vm_milestone_test': 'NOT RUN',
    }
    (evidence / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
