pub const PAGE_SIZE: usize = 4096;

#[repr(C, align(4096))]
pub struct Page {
    pub bytes: [u8; PAGE_SIZE],
}

impl Page {
    pub const fn zeroed() -> Self {
        Self {
            bytes: [0; PAGE_SIZE],
        }
    }
}

const _: () = assert!(core::mem::size_of::<Page>() == PAGE_SIZE);
const _: () = assert!(core::mem::align_of::<Page>() == PAGE_SIZE);

// The exclusive borrow keeps the pool alive and prevents a second allocator
// from accessing it simultaneously. Page indices are not raw physical addresses.
pub struct Pmm<'a> {
    pages: &'a mut [Page],
    next_unused: usize,
    free_head: Option<usize>,
    recycled_count: usize,
}

#[derive(Debug, PartialEq, Eq)]
pub enum FreeError {
    InvalidPage,
    DoubleFree,
}

impl<'a> Pmm<'a> {
    pub fn new(pages: &'a mut [Page]) -> Self {
        Self {
            pages,
            next_unused: 0,
            free_head: None,
            recycled_count: 0,
        }
    }

    // Links occupy the free page's first machine word, avoiding heap metadata.
    fn read_next(&self, index: usize) -> Option<usize> {
        let mut bytes = [0; core::mem::size_of::<usize>()];
        let length = bytes.len();
        bytes.copy_from_slice(&self.pages[index].bytes[..length]);
        usize::from_le_bytes(bytes).checked_sub(1)
    }

    fn write_next(&mut self, index: usize, next: Option<usize>) {
        let bytes = next.map_or(0, |value| value + 1).to_le_bytes();
        self.pages[index].bytes[..bytes.len()].copy_from_slice(&bytes);
    }

    fn is_recycled(&self, index: usize) -> bool {
        let mut current = self.free_head;
        while let Some(page) = current {
            if page == index {
                return true;
            }
            current = self.read_next(page);
        }
        false
    }

    pub fn allocate(&mut self) -> Option<usize> {
        let index = if let Some(page) = self.free_head {
            self.free_head = self.read_next(page);
            self.recycled_count -= 1;
            page
        } else {
            if self.next_unused == self.pages.len() {
                return None;
            }
            let page = self.next_unused;
            self.next_unused += 1;
            page
        };
        self.pages[index].bytes.fill(0);
        Some(index)
    }

    pub fn release(&mut self, index: usize) -> Result<(), FreeError> {
        if index >= self.next_unused {
            return Err(FreeError::InvalidPage);
        }
        if self.is_recycled(index) {
            return Err(FreeError::DoubleFree);
        }
        self.write_next(index, self.free_head);
        self.free_head = Some(index);
        self.recycled_count += 1;
        Ok(())
    }

    pub fn available_pages(&self) -> usize {
        self.pages.len() - self.next_unused + self.recycled_count
    }

    pub fn page_mut(&mut self, index: usize) -> Option<&mut Page> {
        if index >= self.next_unused || self.is_recycled(index) {
            return None;
        }
        Some(&mut self.pages[index])
    }
}
