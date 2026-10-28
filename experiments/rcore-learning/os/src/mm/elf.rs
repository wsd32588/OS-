//! Restricted ELF loading format for this course, not a general ELF loader.
//!
//! Only little-endian ELF64 RISC-V executables with a small set of static LOAD
//! segments are accepted. Sections are irrelevant here; mapping and physical
//! page ownership remain the caller's responsibility.

use alloc::vec::Vec;

const ELF_HEADER_SIZE: usize = 64;
const PROGRAM_HEADER_SIZE: usize = 56;
const MAX_PROGRAM_HEADERS: usize = 32;
const MAX_LOAD_SEGMENTS: usize = 8;
const PAGE_SIZE: usize = 4096;
const LOW_SV39_END: usize = 1usize << 38;
const MAX_MAPPED_BYTES: usize = 128 * 1024;

const PT_LOAD: u32 = 1;
const PT_DYNAMIC: u32 = 2;
const PT_INTERP: u32 = 3;
const PT_TLS: u32 = 7;
const PF_X: u32 = 1;
const PF_W: u32 = 2;
const PF_R: u32 = 4;

pub(super) struct LoadSegment<'a> {
    pub start: usize,
    // The unrounded end: page padding is not part of the segment's contents.
    pub memory_end: usize,
    pub data: &'a [u8],
    pub flags: u32,
}

pub(super) struct ElfImage<'a> {
    pub entry: usize,
    pub segments: Vec<LoadSegment<'a>>,
}

fn file_range(data: &[u8], offset: usize, length: usize) -> Result<&[u8], &'static str> {
    let end = offset.checked_add(length).ok_or("ELF file range overflow")?;
    data.get(offset..end).ok_or("ELF file range out of bounds")
}

fn read_u16(data: &[u8], offset: usize) -> Result<u16, &'static str> {
    let bytes: [u8; 2] = file_range(data, offset, 2)?
        .try_into().map_err(|_| "invalid ELF u16 field")?;
    Ok(u16::from_le_bytes(bytes))
}

fn read_u32(data: &[u8], offset: usize) -> Result<u32, &'static str> {
    let bytes: [u8; 4] = file_range(data, offset, 4)?
        .try_into().map_err(|_| "invalid ELF u32 field")?;
    Ok(u32::from_le_bytes(bytes))
}

fn read_usize(data: &[u8], offset: usize) -> Result<usize, &'static str> {
    let bytes: [u8; 8] = file_range(data, offset, 8)?
        .try_into().map_err(|_| "invalid ELF u64 field")?;
    usize::try_from(u64::from_le_bytes(bytes)).map_err(|_| "ELF value does not fit usize")
}

pub(super) fn parse(data: &[u8]) -> Result<ElfImage<'_>, &'static str> {
    let header = file_range(data, 0, ELF_HEADER_SIZE)?;
    if header.get(..4) != Some(b"\x7fELF".as_slice()) ||
        header.get(4) != Some(&2) || header.get(5) != Some(&1) ||
        header.get(6) != Some(&1) {
        return Err("expected little-endian ELF64 version 1");
    }
    if read_u16(header, 16)? != 2 || read_u16(header, 18)? != 243 ||
        read_u32(header, 20)? != 1 {
        return Err("expected a RISC-V ET_EXEC version 1 image");
    }
    if usize::from(read_u16(header, 52)?) != ELF_HEADER_SIZE ||
        usize::from(read_u16(header, 54)?) != PROGRAM_HEADER_SIZE {
        return Err("unsupported ELF header sizes");
    }

    let entry = read_usize(header, 24)?;
    let program_offset = read_usize(header, 32)?;
    let program_count = usize::from(read_u16(header, 56)?);
    if program_count == 0 || program_count > MAX_PROGRAM_HEADERS {
        return Err("unsupported ELF program header count");
    }
    let program_bytes = program_count.checked_mul(PROGRAM_HEADER_SIZE)
        .ok_or("ELF program header size overflow")?;
    let program_headers = file_range(data, program_offset, program_bytes)?;

    let mut segments = Vec::new();
    segments.try_reserve_exact(MAX_LOAD_SEGMENTS)
        .map_err(|_| "ELF segment metadata allocation failed")?;
    let mut mapped_bytes = 0usize;
    let mut executable_entry = false;

    for program in program_headers.chunks_exact(PROGRAM_HEADER_SIZE) {
        match read_u32(program, 0)? {
            PT_DYNAMIC | PT_INTERP | PT_TLS => {
                return Err("dynamic linking, interpreters and TLS are unsupported");
            }
            PT_LOAD => {}
            _ => { continue; }
        }
        if segments.len() == MAX_LOAD_SEGMENTS {
            return Err("too many ELF LOAD segments");
        }

        let flags = read_u32(program, 4)?;
        let offset = read_usize(program, 8)?;
        let start = read_usize(program, 16)?;
        let file_size = read_usize(program, 32)?;
        let memory_size = read_usize(program, 40)?;
        let alignment = read_usize(program, 48)?;
        if memory_size == 0 || file_size > memory_size {
            return Err("invalid ELF LOAD sizes");
        }
        // Validate the source even for filesz=0 before considering a VA range.
        let contents = file_range(data, offset, file_size)?;
        let memory_end = start.checked_add(memory_size).ok_or("ELF LOAD address overflow")?;
        let rounded_end = memory_end.checked_add(PAGE_SIZE - 1)
            .ok_or("ELF LOAD page rounding overflow")? & !(PAGE_SIZE - 1);
        if start == 0 || rounded_end >= LOW_SV39_END {
            return Err("ELF LOAD is outside the supported low VA range");
        }
        if alignment > 1 && (!alignment.is_power_of_two() ||
            offset % alignment != start % alignment) {
            return Err("invalid ELF LOAD alignment");
        }
        if offset % PAGE_SIZE != start % PAGE_SIZE {
            return Err("ELF LOAD file and VA page offsets differ");
        }
        if flags & !(PF_R | PF_W | PF_X) != 0 || flags & (PF_R | PF_X) == 0 ||
            (flags & PF_W != 0 && flags & PF_R == 0) {
            return Err("invalid ELF LOAD permissions");
        }

        let rounded_start = start & !(PAGE_SIZE - 1);
        mapped_bytes = mapped_bytes.checked_add(rounded_end - rounded_start)
            .ok_or("ELF mapped size overflow")?;
        if mapped_bytes > MAX_MAPPED_BYTES {
            return Err("ELF LOAD mappings exceed the course size limit");
        }
        // A jump into rounded page padding must not qualify as a valid entry.
        if flags & PF_X != 0 && start <= entry && entry < memory_end {
            executable_entry = true;
        }
        segments.push(LoadSegment { start, memory_end, data: contents, flags });
    }

    if segments.is_empty() {
        return Err("ELF image has no LOAD segment");
    }
    if !executable_entry {
        return Err("ELF entry is outside executable LOAD contents");
    }
    Ok(ElfImage { entry, segments })
}
