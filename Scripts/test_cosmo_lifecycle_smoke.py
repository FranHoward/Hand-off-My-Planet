"""Verify the service using only the deployed interpreter and resources."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, default=Path(__file__).resolve().parents[1] / 'Plugins/CosmoBridge/Resources/Runtime/Win64')
    parser.add_argument('--log-dir', type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    python = str(runtime / 'python/python.exe')
    launcher = str(runtime / 'app/cosmo_service.py')
    env = dict(os.environ, COSMO_AI_PROVIDER='mock')
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    parent_code = ("import subprocess,sys,time,os; "
                   "p=subprocess.Popen([sys.argv[1],'-B',sys.argv[2],'--parent-pid',str(os.getpid()),"
                   "'--log-dir',sys.argv[3],'--port',sys.argv[4]]); "
                   "print(p.pid,flush=True); time.sleep(30)")
    parent = subprocess.Popen([python, '-B', '-c', parent_code, python, launcher,
                               str(args.log_dir.resolve()), str(port)], env=env,
                              stdout=subprocess.PIPE, text=True)
    address = f'http://127.0.0.1:{port}/health'
    try:
        child = int(parent.stdout.readline())
        for attempt in range(60):
            try:
                with urllib.request.urlopen(address, timeout=.5) as response:
                    health = json.load(response)
                assert health['ok'] and health['service'] == 'ai-companion-tutorial'
                break
            except OSError:
                time.sleep(.1)
        else:
            raise AssertionError('Bundled COSMO startup timed out')
        parent.terminate()
        parent.wait(timeout=5)
        for attempt in range(40):
            try:
                urllib.request.urlopen(address, timeout=.2).close()
            except OSError:
                break
            time.sleep(.1)
        else:
            raise AssertionError('Service survived owner exit')
        print('PASS: bundled startup, health readiness, owner-exit cleanup')
    finally:
        if parent.poll() is None:
            parent.terminate()
            parent.wait(timeout=5)


if __name__ == '__main__':
    main()
