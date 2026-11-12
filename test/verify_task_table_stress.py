#!/usr/bin/env python3
"""Run real USER lifecycle stress in an isolated RV64 kernel copy."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRECTORIES = ('include', 'kernel', 'user', 'drivers', 'lib', 'freestanding')


def source_hashes():
    paths = [ROOT / 'Makefile', ROOT / 'linker.ld']
    for directory in SOURCE_DIRECTORIES:
        paths += [path for path in (ROOT / directory).rglob('*') if path.is_file() and
                  path.suffix not in ('.o', '.d', '.elf', '.bin', '.su') and
                  'build' not in path.relative_to(ROOT).parts]
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in paths}


def replace_once(path, before, after):
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f'{path}: expected exactly one replacement anchor: {before[:80]!r}')
    path.write_text(text.replace(before, after, 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rounds', type=int, default=500)
    parser.add_argument('--timeout', type=int, default=120,
                        help='maximum QEMU run seconds, excluding build')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 10000 or args.timeout < 1:
        parser.error('rounds must be 1..10000 and timeout must be positive')
    output = args.output_dir or ROOT / '.cache/task-table-stress' / (
        time.strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}')
    output = output.resolve()
    if output.exists():
        parser.error('output directory already exists; select a new path to preserve previous evidence')
    output.mkdir(parents=True)
    work = output / 'source'
    work.mkdir()
    before = source_hashes()
    (output / 'formal-hashes-before.json').write_text(json.dumps(before, indent=2))
    for directory in SOURCE_DIRECTORIES:
        shutil.copytree(ROOT / directory, work / directory,
                        ignore=shutil.ignore_patterns('*.o', '*.d', '*.elf', '*.bin', '*.su', 'build'))
    for name in ('Makefile', 'linker.ld'):
        shutil.copyfile(ROOT / name, work / name)
    payload = ROOT / 'test/user_stress'
    for name in ('main.c', 'task_factory.c', 'task_factory.h'):
        shutil.copyfile(payload / name, work / 'user' / name)
    replace_once(work / 'Makefile', '$(USER_BUILD_DIR)/main.o\n',
                 '$(USER_BUILD_DIR)/main.o \\\n    $(USER_BUILD_DIR)/task_factory.o\n')
    replace_once(work / 'Makefile', '$(LD) -T linker.ld $(OBJS) -o $@',
                 '$(LD) --wrap=pmm_alloc_page --wrap=pmm_free_page --wrap=syscall_handle '
                 '-T linker.ld $(OBJS) -o $@')
    with (work / 'Makefile').open('a') as stream:
        stream.write(f'\nUSER_CFLAGS += -DTEST_STRESS_ROUNDS={args.rounds}\n'
                     f'CXXFLAGS += -DTEST_STRESS_ROUNDS={args.rounds}\n')
    # Use all five table slots for USERs. The normal two S demos stay untouched.
    boot = work / 'kernel/boot/private/boot_stages.cpp'
    text = boot.read_text()
    start = text.index('    const int task_a_id = task_create(task_a);')
    end = text.index('    if (tinyos::configure_builtin_programs(', start)
    boot.write_text(text[:start] + text[end:])
    with (work / 'kernel/task/sched.cpp').open('a') as stream:
        stream.write('\n' + (payload / 'observer.inc').read_text())
    print(f'Stress evidence: {output}', flush=True)
    build = subprocess.run(['make', '-j4'], cwd=work, capture_output=True, text=True, timeout=120)
    build_text = build.stdout + build.stderr
    (output / 'build.log').write_text(build_text)
    if build.returncode:
        print(build_text[-5000:])
        return 1
    undefined = subprocess.run(['riscv64-linux-gnu-nm', '-u', str(work / 'kernel.elf')],
                               capture_output=True, text=True, check=True)
    layout = subprocess.run(['riscv64-linux-gnu-readelf', '-lW', str(work / 'user/build/demo.elf')],
                            capture_output=True, text=True, check=True)
    (output / 'undefined.log').write_text(undefined.stdout)
    (output / 'user-layout.log').write_text(layout.stdout)
    command = ['qemu-system-riscv64', '-machine', 'virt', '-m', '128M', '-nographic',
               '-bios', 'default', '-kernel', str(work / 'kernel.elf')]
    timed_out = False
    stopped_after_success = False
    started = time.monotonic()
    log_path = output / 'qemu.log'
    with log_path.open('wb') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            last_progress = None
            while process.poll() is None:
                text = log_path.read_text(errors='replace')
                progress = re.findall(r'\[stress-observer\] rounds=(0x[0-9a-f]+)', text)
                if progress and progress[-1] != last_progress:
                    last_progress = progress[-1]
                    print(f'Completed {int(last_progress, 16)}/{args.rounds} rounds', flush=True)
                if '[stress-observer] FAIL' in text or '[stress-user] FAIL' in text or 'kernel halt' in text:
                    break
                marker = f'[stress-observer] PASS rounds=0x{args.rounds:016x}'
                if marker in text and text.rsplit(marker, 1)[1].count('[timer] tick in S-mode') >= 2:
                    stopped_after_success = True
                    break
                if time.monotonic() - started >= args.timeout:
                    timed_out = True
                    break
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=10)
    text = log_path.read_text(errors='replace')
    expected_events = (args.rounds + 3) // 4
    expected_allocations = args.rounds * 35 + expected_events * 7
    allocation_match = re.search(r'\[stress-observer\] runtime_allocations=(0x[0-9a-f]+)', text)
    free_match = re.search(r'\[stress-observer\] runtime_frees=(0x[0-9a-f]+)', text)
    observed_allocations = int(allocation_match[1], 16) if allocation_match else None
    observed_frees = int(free_match[1], 16) if free_match else None
    checks = {
        'target_build_without_diagnostics': not re.search(r'warning:|error:', build_text),
        'no_undefined_symbols': not undefined.stdout.strip(),
        'all_rounds_and_final_root_release':
            text.count(f'[stress-observer] PASS rounds=0x{args.rounds:016x}') == 1,
        'post_exit_timer': stopped_after_success,
        'exact_expected_user_faults': text.count('[task] user fault id=') == expected_events,
        'exact_runtime_page_counts': observed_allocations == expected_allocations and
            observed_frees == expected_allocations + 7,
        'no_observer_or_user_failure': '[stress-observer] FAIL' not in text and
            '[stress-user] FAIL' not in text and 'kernel halt' not in text,
        'formal_sources_unchanged': source_hashes() == before,
        'completed_before_timeout': not timed_out,
    }
    report = {
        'all_passed': all(checks.values()), 'checks': checks,
        'rounds': args.rounds, 'successful_workers': args.rounds * 4,
        'spawn_workers': ((args.rounds + 1) // 2) * 4,
        'fork_workers': (args.rounds // 2) * 4,
        'exec_replacements': (args.rounds + 3) // 4,
        'expected_user_faults': (args.rounds + 3) // 4,
        'expected_full_slot_rejections': args.rounds,
        'runtime_page_allocations': observed_allocations,
        'runtime_page_frees_including_final_root': observed_frees,
        'seconds': round(time.monotonic() - started, 2), 'workspace': str(work),
        'limitations': 'One persistent USER reaper plus four real workers per cohort, all five USER slots '
            'occupied. Test-only boot omits the two S demos. No kernel-task recycling or root restart '
            'policy is added. Existing production spawn/fork/exec/fault/wait paths execute in the copy; '
            'read-only syscall checkpoints and PMM observers measure it. QEMU is stopped externally '
            'after success plus two S-mode idle timer interrupts.',
    }
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['all_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
