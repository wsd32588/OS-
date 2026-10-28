//! Two independent user roots selected by a shared kernel event loop.
use crate::task::manager::TaskStatus::{self,Running};

use super::address::{PAGE_SIZE, PhysAddr, VirtAddr};
use super::frame_allocator::{self, FrameTracker};
use super::heap_allocator;
use super::memory_set::MemorySet;
use super::page_table::{PTE_A, PTE_D, PTE_R, PTE_U, PTE_V, PTE_W, PTE_X};
use super::paged_syscall::{self, Outcome};
use super::paged_user::{self, PagedRuntime, PagedTrapContext, StartupState};
use super::trampoline::{self, TRAMPOLINE};
use super::user_image::UserImage;

const TASK_COUNT: usize = 2;
const SEED_ADDRESS: usize = 0x4000_2000;
const UNKNOWN_SYSCALL: usize = 0xffff;
static IMAGE: &[u8] = include_bytes!("../../../user/target/paged/riscv64gc-unknown-none-elf/release/task_demo.elf");

// Each image root dies before its borrowed context page.
pub(super) struct PagedTask {
    pub(super) image: UserImage,
    pub(super) context: FrameTracker,
    pub(super) context_va: usize,
    pub(super) status: TaskStatus,
}

fn context_address(id: usize) -> Result<usize, &'static str> {
    if id >= TASK_COUNT {
        return Err("invalid paged task id");
    }
    Ok(TRAMPOLINE - (id + 1) * PAGE_SIZE)
}

impl PagedTask {
    fn from_elf(
        id: usize, bytes: &[u8], status: usize, kernel_satp: usize,
    ) -> Result<Self, &'static str> {
        let context_va = context_address(id)?;
        let mut context = frame_allocator::frame_alloc().ok_or("no frame for task context")?;
        let mut image = UserImage::from_elf(bytes)?;
        image.memory.map_trampoline()?;
        // SAFETY: Image owns its root and is destroyed before its context.
        unsafe { image.memory.map_paged_context_at(context_va, context.ppn())?; }
        image.memory.copy_data(VirtAddr::new(SEED_ADDRESS).ok_or("invalid seed VA")?, &[id as u8])?;
        let initial = PagedTrapContext::initial(image.entry, image.stack_top, status, kernel_satp);
        paged_user::initialize_context_page(&mut context, initial);
        Ok(Self { image, context, context_va, status: TaskStatus::Ready })
    }

    fn new(id: usize, status: usize, kernel_satp: usize) -> Result<Self, &'static str> {
        let context_va = context_address(id)?;
        // BATCH21 A BEGIN
        // Declare the context frame FIRST, then image=UserImage::from_elf(IMAGE).
        // Map the trampoline and this context slot into image.memory. The
        // unsafe map_paged_context_at borrow is protected by image-before-context
        // field destruction (and reverse local destruction on an early Err).
        // Seed SEED_ADDRESS with [id as u8] via copy_data. Build initial with
        // image.entry/image.stack_top/status/kernel_satp, then initialize the
        // owned context with paged_user::initialize_context_page. Return Self
        // with status Ready. Do not map this context into the kernel here: the
        // containing session does that only after it owns both tasks safely.

        // 先声明现场页：局部变量逆序销毁，所以 image 的页表会先于它释放。
        let mut context = frame_allocator::frame_alloc()
            .ok_or("no frame for the paged task context")?;
        let mut image = UserImage::from_elf(IMAGE)?;
        image.memory.map_trampoline()?;

        // SAFETY: 现场帧由返回值持有，映射只记录页号；局部与字段的销毁顺序
        // 都保证用户页表先于现场页释放。
        unsafe {
            image.memory.map_paged_context_at(context_va,context.ppn())?;
        }

        // 每个任务在 SEED_ADDRESS 写自己的 id，用来验证地址空间互相独立。
        let seed = VirtAddr::new(SEED_ADDRESS).ok_or("invalid seed VA")?;
        image.memory.copy_data(seed, &[id as u8])?;

        let initial = PagedTrapContext::initial(
            image.entry, image.stack_top, status, kernel_satp);
        paged_user::initialize_context_page(&mut context, initial);

        Ok(Self {
            image,
            context,
            context_va,
            status: TaskStatus::Ready,
        })// BATCH21 A END
    }
}

#[derive(Clone, Copy)]
pub(super) enum ScheduleEvent {
    Yield,
    Exit,
}

// First destroy the shared kernel root, then each image root/context owner.
pub(super) struct PagedTasks {
    pub(super) kernel: MemorySet,
    pub(super) manager: PagedTaskManager,
}

pub(super) struct PagedTaskManager {
    pub(super) tasks: [PagedTask; TASK_COUNT],
    pub(super) current: usize,
}

impl PagedTasks {
    pub(super) fn from_elf(bytes: &[u8], status: usize) -> Result<Self, &'static str> {
        let mut kernel = MemorySet::new_kernel()?;
        kernel.map_trampoline()?;
        let first = PagedTask::from_elf(0, bytes, status, kernel.token())?;
        let second = PagedTask::from_elf(1, bytes, status, kernel.token())?;
        let mut session = Self { kernel, manager: PagedTaskManager { tasks: [first, second], current: 0 } };
        // Publish aliases only after the enclosing owner protects destruction
        // order on both success and an error while mapping the second slot.
        for task in &session.manager.tasks {
            unsafe { session.kernel.map_paged_context_at(task.context_va, task.context.ppn())?; }
        }
        Ok(session)
    }

    fn new(status: usize) -> Result<Self, &'static str> {
        let mut kernel = MemorySet::new_kernel()?;
        kernel.map_trampoline()?;
        // Until session construction, the kernel has no task-context aliases.
        // Task1 failure can therefore drop task0 before this local kernel.
        let first = PagedTask::new(0, status, kernel.token())?;
        let second = PagedTask::new(1, status, kernel.token())?;
        let mut session = Self { kernel, manager: PagedTaskManager { tasks: [first, second], current: 0 } };
        for task in &session.manager.tasks {
            // SAFETY: The session now owns all pages, with the kernel root
            // first in field destruction order even when the second map fails.
            unsafe { session.kernel.map_paged_context_at(task.context_va, task.context.ppn())?; }
        }
        Ok(session)
    }

}

impl PagedTaskManager {
    fn find_next_ready(&self) -> Option<usize> {
        // BATCH21 B BEGIN
        // Inspect exactly TASK_COUNT candidates, offsets 1..=TASK_COUNT.
        // id=(self.current+offset)%TASK_COUNT; accept only status Ready.
        // The current task is the LAST candidate, which permits a lone task
        // to yield to itself after its peer has exited. Return None if none.
        for offset in 1..=TASK_COUNT {
            let next = (self.current + offset) % TASK_COUNT;
            if self.tasks[next].status == TaskStatus::Ready {
                return Some(next)
            }
        }

        None
        // BATCH21 B END
    }

    pub(super) fn schedule_after_trap(&mut self, event: ScheduleEvent) -> Option<(usize, usize)> {
        assert_eq!(self.tasks[self.current].status, TaskStatus::Running);
        // BATCH21 C BEGIN
        // Save the old id. Yield changes its Running status to Ready; Exit to
        // Exited. Call find_next_ready; if found, mark it Running, update current
        // and return Some((old,next)); otherwise return None. Exited is never
        // eligible again. Do not modify a saved user context in this method.
        let old_id = self.current;

        match event {
            ScheduleEvent::Yield => {
                self.tasks[old_id].status = TaskStatus::Ready;
            },
            ScheduleEvent::Exit => {
                self.tasks[old_id].status = TaskStatus::Exited;
            }
        }

        match self.find_next_ready() {
            Some(next) => {
                self.tasks[next].status = Running;
                self.current = next;
                
                return Some((old_id, next))
            },
            None => {
                return None
            }
        }
    }
        // BATCH21 C END
}


fn verify_shared_mapping(
    memory: &MemorySet, base: usize, physical: usize, permissions: u64,
) -> Result<(), &'static str> {
    for offset in [0, PAGE_SIZE - 1] {
        let va = VirtAddr::new(base + offset).ok_or("invalid shared VA")?;
        if memory.translate_va(va).map(PhysAddr::as_usize) != Some(physical + offset) ||
            memory.leaf_flags(va) != Some(permissions | PTE_V) {
            return Err("paged task shared mapping mismatch");
        }
    }
    Ok(())
}

pub(super) fn verify_session(session: &PagedTasks) -> Result<(), &'static str> {
    session.kernel.verify_kernel_mappings()?;
    assert_eq!(session.kernel.table_frame_count(), 11);
    let shared_pa = trampoline::physical_page()?.to_addr().as_usize();
    verify_shared_mapping(&session.kernel, TRAMPOLINE, shared_pa, PTE_R | PTE_X | PTE_A)?;
    let mut data_addresses = [0; TASK_COUNT];
    for (id, task) in session.manager.tasks.iter().enumerate() {
        assert_eq!(task.status, TaskStatus::Ready);
        assert_eq!(task.image.memory.table_frame_count(), 5);
        assert_eq!(task.image.memory.owned_frame_count(), 6);
        assert_eq!(task.image.entry, 0x4000_0000);
        assert_eq!(task.image.guard_start, 0x4000_4000);
        assert_eq!(task.context_va, context_address(id)?);
        let entry = VirtAddr::new(task.image.entry).ok_or("invalid task entry")?;
        if task.image.memory.leaf_flags(entry) != Some(PTE_V | PTE_R | PTE_X | PTE_U | PTE_A) {
            return Err("task entry must be executable user memory");
        }
        verify_shared_mapping(&task.image.memory, TRAMPOLINE, shared_pa, PTE_R | PTE_X | PTE_A)?;
        let context_pa = task.context.ppn().to_addr().as_usize();
        let context_permissions = PTE_R | PTE_W | PTE_A | PTE_D;
        verify_shared_mapping(&task.image.memory, task.context_va, context_pa, context_permissions)?;
        verify_shared_mapping(&session.kernel, task.context_va, context_pa, context_permissions)?;
        let other = VirtAddr::new(context_address(1 - id)?).ok_or("invalid other slot")?;
        if task.image.memory.translate_va(other).is_some() {
            return Err("task root unexpectedly maps its peer's context");
        }
        let seed = VirtAddr::new(SEED_ADDRESS).ok_or("invalid seed address")?;
        if task.image.memory.owned_byte(seed) != Some(id as u8) {
            return Err("task identity was not seeded in its private data");
        }
        data_addresses[id] = task.image.memory.translate_va(seed)
            .ok_or("missing task data")?.as_usize();
        let sp: usize;
        unsafe { core::arch::asm!("mv {}, sp", out(reg) sp, options(nomem, nostack)); }
        for raw in [sp, run_demo as *const () as usize] {
            if task.image.memory.translate_va(VirtAddr::new(raw).ok_or("invalid kernel VA")?).is_some() {
                return Err("user root maps low kernel stack/code");
            }
        }
    }
    assert_ne!(data_addresses[0], data_addresses[1]);
    assert_ne!(session.manager.tasks[0].context.ppn(), session.manager.tasks[1].context.ppn());
    assert_ne!(session.manager.tasks[0].image.memory.token(), session.manager.tasks[1].image.memory.token());
    Ok(())
}

pub(super) fn run_demo() -> Result<(), &'static str> {
    // This temporary experiment has no timer and retains one shared kernel
    // event-loop continuation. User contexts/roots alone are cooperatively
    // selected; the old TaskContext/__switch scheduler is still untouched.
    let initial = StartupState::capture()?;
    let frames_before = frame_allocator::statistics().ok_or("frame allocator not initialized")?;
    let heap_before = heap_allocator::statistics();
    let mut session = PagedTasks::new(initial.status())?;
    verify_session(&session)?;
    let frames_mapped = frame_allocator::statistics().expect("frame allocator");
    let heap_mapped = heap_allocator::statistics();
    assert_eq!(frames_mapped.in_use, frames_before.in_use + 35);
    crate::println!("[paged-task] tasks=2 same_VA/different_PA=true context_slots=2 isolated=true");
    crate::println!("[paged-task] kernel_tables=11 each_user_tables=5 each_user_data=6 contexts=2 in_use_delta=35");
    let expected_events = [
        (0, UNKNOWN_SYSCALL), (0, 64), (0, 124), (1, 64), (1, 124),
        (0, 64), (0, 124), (1, 64), (1, 124), (0, 93),
        (1, 64), (1, 124), (1, 93),
    ];
    let (mut traps, mut resumes, mut writes, mut yields, mut exits, mut unknown) = (0, 0, 0, 0, 0, 0);
    let mut self_yields = 0;
    let mut rounds = [0; TASK_COUNT];
    session.manager.tasks[0].status = TaskStatus::Running;
    {
        // SAFETY: Both roots/slots were verified and their owners outlive the
        // runtime. SIE/sie are zero. No Rust page reference crosses invoke.
        let mut runtime = unsafe { PagedRuntime::enter(&session.kernel)? };
        loop {
            if traps >= expected_events.len() {
                return Err("paged tasks exceeded the thirteen-trap fixture bound");
            }
            let id = session.manager.current;
            assert_eq!(session.manager.tasks[id].status, TaskStatus::Running);
            let task = &mut session.manager.tasks[id];
            let observed = unsafe { runtime.invoke(task.context_va, &task.image, &task.context)? };
            let mut context = runtime.saved_context(&task.context);
            runtime.verify_user_trap(observed, task.image.memory.token(), &context, &task.image)?;
            assert_eq!((id, context.x[17]), expected_events[traps]);
            traps += 1;
            let counter = VirtAddr::new(SEED_ADDRESS + 1).ok_or("invalid counter VA")?;
            match context.x[17] {
                124 => {
                    yields += 1;
                    paged_syscall::prepare_resume(&mut context, 0)?;
                    runtime.replace_context(&mut task.context, context);
                    resumes += 1;
                    // End this task borrow before changing the scheduler state.
                    let _ = task;
                    let transition = session.manager.schedule_after_trap(ScheduleEvent::Yield)
                        .ok_or("a yielding task must remain runnable")?;
                    if transition.0 == transition.1 {
                        self_yields += 1;
                    }
                    crate::println!("[paged-task] yield {} -> {}", transition.0, transition.1);
                }
                _ => match paged_syscall::dispatch(&task.image.memory, &context) {
                    Outcome::Return(result) => {
                        if context.x[17] == 64 {
                            let round = rounds[id];
                            let message = fixture_message(id, round)?;
                            assert_eq!(context.x[10], 1);
                            assert_eq!(context.x[12], message.len());
                            assert_eq!(result, message.len() as isize);
                            let mut bytes = [0u8; 32];
                            paged_syscall::copy_readable_user(&task.image.memory,
                                context.x[11], &mut bytes[..message.len()])?;
                            assert_eq!(&bytes[..message.len()], message);
                            assert_eq!(task.image.memory.owned_byte(counter), Some(10 + id as u8 * 10 + round as u8));
                            rounds[id] += 1;
                            writes += 1;
                        } else {
                            assert_eq!(context.x[17], UNKNOWN_SYSCALL);
                            assert_eq!(result, -1);
                            unknown += 1;
                            crate::println!("[paged-task] unknown=Return(-1) current=0 unchanged=true");
                        }
                        paged_syscall::prepare_resume(&mut context, result)?;
                        runtime.replace_context(&mut task.context, context);
                        resumes += 1;
                        // write and unknown return to the same Running task.
                    }
                    Outcome::Exit(code) => {
                        assert_eq!(code, 0);
                        exits += 1;
                        crate::println!("[paged-task] exit {} code=0", id);
                        let _ = task;
                        if let Some((from, to)) = session.manager.schedule_after_trap(ScheduleEvent::Exit) {
                            crate::println!("[paged-task] exit {} -> {}", from, to);
                        } else {
                            break;
                        }
                    }
                },
            }
            assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
            assert_eq!(heap_allocator::statistics(), heap_mapped);
        }
    }
    assert_eq!((traps, resumes, writes, yields, exits, unknown, self_yields), (13, 11, 5, 5, 2, 1, 1));
    assert_eq!(rounds, [2, 3]);
    assert!(session.manager.tasks.iter().all(|task| task.status == TaskStatus::Exited));
    initial.assert_restored();
    assert_eq!(frame_allocator::statistics(), Some(frames_mapped));
    assert_eq!(heap_allocator::statistics(), heap_mapped);
    crate::println!("[paged-task] U_ecalls=13 resumes=11 writes=5 yields=5 exits=2 unknown=1 self_yield=1");
    drop(session);
    assert_eq!(frame_allocator::statistics(), Some(frames_before));
    assert_eq!(heap_allocator::statistics(), heap_before);
    crate::println!("[paged-task] restored: satp=Bare CSR=true free={} in_use={} heap_restored=true",
        frames_before.free, frames_before.in_use);
    Ok(())
}

fn fixture_message(id: usize, round: usize) -> Result<&'static [u8], &'static str> {
    match (id, round) {
        (0, 0) => Ok(b"[paged-task0] round0\n"),
        (0, 1) => Ok(b"[paged-task0] round1\n"),
        (1, 0) => Ok(b"[paged-task1] round0\n"),
        (1, 1) => Ok(b"[paged-task1] round1\n"),
        (1, 2) => Ok(b"[paged-task1] round2\n"),
        _ => Err("unexpected fixture round"),
    }
}
