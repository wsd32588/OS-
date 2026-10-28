pub const PAGE_SHIFT: u32 = 12;
pub const LEVEL_COUNT: u32 = 3;
pub const INVALID_INDEX: u32 = 512;
pub const FLAG_MASK: u64 = 0xff;
pub const PPN_MASK: u64 = ((1 << 44) - 1) << 10;
pub const VALID: u64 = 1;
pub const READ: u64 = 2;
pub const WRITE: u64 = 4;
pub const EXECUTE: u64 = 8;

pub const fn is_canonical(address: u64) -> bool {
    let upper = address >> 39;
    let sign = (address >> 38) & 1;
    upper == if sign == 0 { 0 } else { (1 << 25) - 1 }
}

pub const fn vpn_index(address: u64, level: u32) -> u32 {
    if level >= LEVEL_COUNT {
        return INVALID_INDEX;
    }
    ((address >> (PAGE_SHIFT + level * 9)) & 511) as u32
}

pub const fn make_pte(physical_address: u64, flags: u64) -> u64 {
    (((physical_address >> PAGE_SHIFT) << 10) & PPN_MASK) | (flags & FLAG_MASK)
}

pub const fn physical_address(pte: u64) -> u64 {
    ((pte & PPN_MASK) >> 10) << PAGE_SHIFT
}

pub const fn flags(pte: u64) -> u64 {
    pte & FLAG_MASK
}

pub const fn is_valid(pte: u64) -> bool {
    pte & VALID != 0 && !(pte & READ == 0 && pte & WRITE != 0)
}

pub const fn is_leaf(pte: u64) -> bool {
    is_valid(pte) && pte & (READ | EXECUTE) != 0
}
