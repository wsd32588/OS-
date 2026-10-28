use crate::loader::{APP_COUNT, START_APP_ID};

use super::context::TaskContext;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskStatus {
    Uninitialized,
    Ready,
    Running,
    Exited,
}

pub struct TaskControlBlock {
    pub status: TaskStatus,
    pub task_context: TaskContext,
}

pub struct TaskManager {
    pub tasks: [TaskControlBlock; APP_COUNT],
    pub current: usize,
    pub initialized: bool,
    pub had_failure: bool,
}

impl TaskManager {
    pub const fn empty() -> Self {
        Self {
            tasks: [const { TaskControlBlock {
                status: TaskStatus::Uninitialized,
                task_context: TaskContext { ra: 0, sp: 0, s: [0; 12] },
            } }; APP_COUNT],
            current: START_APP_ID,
            initialized: false,
            had_failure: false,
        }
    }

    pub fn current_task_id(&self) -> usize {
        assert!(self.initialized);
        assert_eq!(self.tasks[self.current].status, TaskStatus::Running);
        self.current
    }

    pub fn find_next_task(&self) -> Option<usize> {
        assert!(self.initialized);
        // BATCH08 A：按环形顺序恰好检查 APP_COUNT 个候选，
        // 从 current 之后开始，最后才轮到 current 自己；只有 Ready 有资格。
        let mut no:usize;
        for offset in  1..=APP_COUNT {
            no = (self.current + offset) % APP_COUNT;
            if self.tasks[no].status == TaskStatus::Ready {
                return Some(no);
            }
        }
        return None
    }

    pub fn start_first(&mut self) -> usize {
        assert!(self.initialized);
        assert_eq!(self.tasks[self.current].status, TaskStatus::Ready);
        // BATCH08 B：把选中的首个任务标为 Running 并返回它的 id。

        self.tasks[self.current].status = TaskStatus::Running;
        self.current
    }

    pub fn suspend_current(&mut self) -> (usize, usize) {
        let current = self.current_task_id();
        // BATCH08 C：Running -> Ready；找下一个（允许选回自己），再把它
        // 标为 Running、更新 current，返回（原 id, 新 id）。
        if  self.tasks[current].status != TaskStatus::Ready {
            self.tasks[current].status = TaskStatus::Ready;
        }
        if let Some(next) = self.find_next_task() {
            self.tasks[next].status = TaskStatus::Running;
            self.current = next;
            (current, next)
        } else {
            self.tasks[current].status = TaskStatus::Running;
            (current, current)
        }

    }

    pub fn exit_current(&mut self, exit_code: i32) -> Option<(usize, usize)> {
        let current = self.current_task_id();
        // BATCH08 D：把 current 标为 Exited；记住非零退出码；选一个
        // Ready 任务并激活它；所有任务都退出时返回 None。
        self.tasks[current].status = TaskStatus::Exited;
        self.had_failure = self.had_failure || exit_code != 0;

        if let Some(next) = self.find_next_task() {
            self.tasks[next].status = TaskStatus::Running;
            self.current = next;
            Some((current,next))
        } else {
            None
        }
    
    }
}
