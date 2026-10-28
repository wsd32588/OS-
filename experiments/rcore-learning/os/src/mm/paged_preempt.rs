//! Timers select independent user roots; the shared kernel stays non-preemptible.
use crate::mm::paged_syscall::Outcome::Return;
use crate::mm::paged_syscall::prepare_resume;
use crate::task::manager::TaskStatus;
use crate::timer::set_next_trigger;

use super::address::VirtAddr;
use super::frame_allocator;
use super::heap_allocator;
use super::paged_syscall;
use super::paged_task::{self, PagedTaskManager, PagedTasks, ScheduleEvent};
use super::paged_user::{PagedRuntime, PagedTrapContext, SIE_STIE, StartupState};


const INTERRUPT_BIT: usize = 1usize << (usize::BITS - 1);
const TIMER_CAUSE: usize = INTERRUPT_BIT | 5;
const DATA_ADDRESS: usize = 0x4000_2000;
const TIMER_LIMIT: usize = 128;
static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/preempt_demo.elf");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum TrapKind {
    UserEcall,
    Timer,
}

#[derive(Clone, Copy)]
enum EcallAction {
    Resume,
    Exit { code: isize, transition: Option<(usize, usize)> },
}

fn decode_cause(scause: usize) -> Result<TrapKind, &'static str> {
    // BATCH22 A BEGIN
    // Accept EXACTLY 8 as UserEcall and TIMER_CAUSE as Timer. All other values
    // return Err, including synchronous exception code 5 with no interrupt bit.
    match scause {
        8 => {
            return Ok(TrapKind::UserEcall)
        },
        TIMER_CAUSE => {
            return Ok(TrapKind::Timer)
        },
        _ => {
            return Err("can not match any system-call");
        }
    }
    // BATCH22 A END
}

fn preempt_current(manager: &mut PagedTaskManager) -> Result<(usize, usize), &'static str> {
    // BATCH22 B BEGIN
    // Arm crate::timer::set_next_trigger()? BEFORE choosing another task.
    // Reuse schedule_after_trap(ScheduleEvent::Yield), converting unexpected
    // None to Err. Do not edit any saved context, sepc, or a0 on a timer.
    set_next_trigger()?;

    let (old, curr) = manager
        .schedule_after_trap(ScheduleEvent::Yield)
        .ok_or("a preempted task must remain runnable")?;

    Ok((old, curr))
    // BATCH22 B END
}

fn handle_ecall(
    manager: &mut PagedTaskManager,
    context: &mut PagedTrapContext,
) -> Result<EcallAction, &'static str> {
    // BATCH22 C BEGIN
    // Dispatch against manager.tasks[manager.current].image.memory using B20.
    // Return(result): prepare_resume(context,result)? and Resume the SAME task.
    // Exit(code): schedule_after_trap(Exit), returning Exit {code,transition}.
    // Exit never advances sepc or restores this application's context.
    
    let curr = manager.current;
    let memory = &manager.tasks[curr].image.memory;
    match paged_syscall::dispatch(memory, context) {
        Return(result) => {
            prepare_resume(context, result)?;
            if manager.tasks[curr].status != TaskStatus::Running {
                return Err("the task is not Running")
            }
            return Ok(EcallAction::Resume);
        },
        paged_syscall::Outcome::Exit(code) => {
            let transition = manager.schedule_after_trap(ScheduleEvent::Exit);

            return Ok(
                EcallAction::Exit { code, transition }
            );
        }
    }
    // BATCH22 C END
}

fn progress(manager: &PagedTaskManager, id: usize) -> Result<u32, &'static str> {
    // Fixture-only repr(C) data: byte0=id, byte1=stop, counter=u32 at byte4.
    // The user is paused and the checked kernel identity root or Bare is active.
    let memory = &manager.tasks[id].image.memory;
    let mut bytes = [0u8; 4];
    for (offset, byte) in bytes.iter_mut().enumerate() {
        let va = VirtAddr::new(DATA_ADDRESS + 4 + offset).ok_or("invalid progress VA")?;
        *byte = memory.owned_byte(va).ok_or("missing private progress byte")?;
    }
    Ok(u32::from_le_bytes(bytes))
}

fn stop_fixture_task(manager: &mut PagedTaskManager, id: usize) -> Result<(), &'static str> {
    // Controlled fixture termination is deliberately outside scheduler policy.
    // copy_data writes the owning physical frame; no raw user VA is dereferenced.
    let va = VirtAddr::new(DATA_ADDRESS + 1).ok_or("invalid stop VA")?;
    manager.tasks[id].image.memory.copy_data(va, &[1])
}

fn verify_ecall(context: &PagedTrapContext, manager: &PagedTaskManager) -> Result<(), &'static str> {
    let memory = &manager.tasks[manager.current].image.memory;
    for (offset, expected) in [0x73, 0, 0, 0].into_iter().enumerate() {
        let va = VirtAddr::new(context.sepc.checked_add(offset).ok_or("ecall PC overflow")?)
            .ok_or("invalid ecall VA")?;
        if memory.owned_byte(va) != Some(expected) {
            return Err("preempt fixture did not stop at ecall");
        }
    }
    Ok(())
}

pub(super) fn run_demo() -> Result<(), &'static str> {
    let initial = StartupState::capture()?;
    let frames_before = frame_allocator::statistics().ok_or("frame allocator not initialized")?;
    let heap_before = heap_allocator::statistics();
    let mut session = PagedTasks::from_elf(IMAGE, initial.status())?;
    paged_task::verify_session(&session)?;
    let frames_mapped = frame_allocator::statistics().expect("frame allocator");
    let heap_mapped = heap_allocator::statistics();
    assert_eq!(frames_mapped.in_use, frames_before.in_use + 35);
    crate::println!("[paged-preempt] tasks=2 isolated=true in_use_delta=35");

    // Exactly one rejection, before activating paging or arming the timer.
    assert!(decode_cause(5).is_err());
    initial.assert_restored();
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-preempt] cause5=Err state/resources_unchanged=true");

    let mut timers = 0;
    let mut timer_resumes = 0;
    let mut ecalls = 0;
    let mut ecall_resumes = 0;
    let mut writes = 0;
    let mut exits = 0;
    let mut directions = [false; 2];
    let mut self_timer = false;
    let mut stopped = [false; 2];
    let mut progressed = [false; 2];
    let mut previous = [progress(&session.manager, 0)?, progress(&session.manager, 1)?];
    session.manager.tasks[0].status = TaskStatus::Running;
    {
        // SIE remains zero throughout the shared kernel loop. STIE is armed
        // only inside this borrowed runtime and canceled before its Drop exits.
        let mut runtime = unsafe { PagedRuntime::enter(&session.kernel)? };
        runtime.enable_timer()?;
        loop {
            if timers >= TIMER_LIMIT || ecalls > 4 {
                return Err("preempt fixture exceeded its finite trap bounds");
            }
            let id = session.manager.current;
            assert_eq!(session.manager.tasks[id].status, TaskStatus::Running);
            let task = &session.manager.tasks[id];
            let observed = unsafe { runtime.invoke(task.context_va, &task.image, &task.context)? };
            let mut context = runtime.saved_context(&task.context);
            let snapshot = runtime.verify_trap_snapshot(observed, task.image.memory.token(),
                &context, &task.image, SIE_STIE)?;
            let kind = decode_cause(snapshot.scause).map_err(|message| {
                crate::println!("[paged-preempt] unexpected cause={:#x} stval={:#x}", snapshot.scause, snapshot.stval);
                message
            })?;
            let current = [progress(&session.manager, 0)?, progress(&session.manager, 1)?];
            assert_eq!(current[1 - id], previous[1 - id], "paused peer data must stay unchanged");
            assert!(current[id] >= previous[id]);
            progressed[id] |= current[id] > 1;
            previous = current;

            match kind {
                TrapKind::Timer => {
                    timers += 1;
                    // Observe both real switching directions before asking
                    // task0 to finish. This avoids guessing how fast a loop runs.
                    if id == 0 && timers >= 3 && directions.iter().all(|seen| *seen) &&
                        progressed[0] && !stopped[0] {
                        stop_fixture_task(&mut session.manager, 0)?;
                        stopped[0] = true;
                    }
                    let saved = [
                        runtime.saved_context(&session.manager.tasks[0].context),
                        runtime.saved_context(&session.manager.tasks[1].context),
                    ];
                    let original = context;
                    let (from, to) = preempt_current(&mut session.manager)?;
                    // The timer helper may change only lifecycle/selection and
                    // the next deadline, never either complete saved user frame.
                    for (index, before) in saved.into_iter().enumerate() {
                        assert!(runtime.saved_context(&session.manager.tasks[index].context) == before);
                    }
                    assert!(context == original);
                    assert_eq!(from, id);
                    if from != to {
                        directions[from] = true;
                    } else {
                        assert_eq!(from, 1);
                        assert_eq!(session.manager.tasks[0].status, TaskStatus::Exited);
                        self_timer = true;
                        if progressed[1] && !stopped[1] {
                            stop_fixture_task(&mut session.manager, 1)?;
                            stopped[1] = true;
                        }
                    }
                    timer_resumes += 1;
                    crate::println!("[paged-preempt] timer {} -> {} context_unchanged=true", from, to);
                }
                TrapKind::UserEcall => {
                    ecalls += 1;
                    verify_ecall(&context, &session.manager)?;
                    if context.x[17] == 64 {
                        let message: &[u8] = if id == 0 {
                            b"[paged-preempt0] done\n"
                        } else {
                            b"[paged-preempt1] done\n"
                        };
                        assert_eq!(context.x[12], message.len());
                        let mut copied = [0u8; 32];
                        paged_syscall::copy_readable_user(&session.manager.tasks[id].image.memory,
                            context.x[11], &mut copied[..message.len()])?;
                        assert_eq!(&copied[..message.len()], message);
                        writes += 1;
                    } else {
                        assert_eq!(context.x[17], 93);
                    }
                    let original = context;
                    match handle_ecall(&mut session.manager, &mut context)? {
                        EcallAction::Resume => {
                            assert_eq!(session.manager.current, id);
                            assert_eq!(session.manager.tasks[id].status, TaskStatus::Running);
                            assert_eq!(context.sepc, original.sepc + 4);
                            assert_eq!(context.x[10], original.x[12]);
                            runtime.replace_context(&mut session.manager.tasks[id].context, context);
                            ecall_resumes += 1;
                        }
                        EcallAction::Exit { code, transition } => {
                            assert_eq!(code, 0);
                            assert!(context == original, "exit never resumes its saved frame");
                            assert_eq!(session.manager.tasks[id].status, TaskStatus::Exited);
                            exits += 1;
                            crate::println!("[paged-preempt] exit {} code=0", id);
                            if let Some((from, to)) = transition {
                                assert_eq!(from, id);
                                assert_eq!(session.manager.current, to);
                            } else {
                                break;
                            }
                        }
                    }
                }
            }
            assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
            assert_eq!(heap_allocator::statistics(), heap_mapped);
        }
    }
    assert!(timers >= 5 && timers <= TIMER_LIMIT);
    assert_eq!(timer_resumes, timers);
    assert_eq!((ecalls, ecall_resumes, writes, exits), (4, 2, 2, 2));
    assert!(directions.iter().all(|seen| *seen));
    assert!(self_timer && stopped.iter().all(|value| *value) && progressed.iter().all(|value| *value));
    assert!(session.manager.tasks.iter().all(|task| task.status == TaskStatus::Exited));
    initial.assert_restored();
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-preempt] timers={} timer_resumes={} U_ecalls=4 ecall_resumes=2 writes=2 exits=2", timers, timer_resumes);
    crate::println!("[paged-preempt] bidirectional=true self_timer=true private_progress=true contexts_preserved=true");
    drop(session);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    crate::println!("[paged-preempt] restored: timer=cancelled satp=Bare CSR=true free={} in_use={} heap_restored=true",
        frames_before.free, frames_before.in_use);
    Ok(())
}
