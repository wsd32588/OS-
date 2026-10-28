pub const PAGE_SIZE_BITS: usize = 12;
pub const PAGE_SIZE: usize = 1 << PAGE_SIZE_BITS;
const VA_BITS: usize = 39;
const VA_MASK: usize = (1usize << VA_BITS) - 1;
const VPN_BITS: usize = VA_BITS - PAGE_SIZE_BITS;
const PA_BITS: usize = 56;
const PPN_BITS: usize = PA_BITS - PAGE_SIZE_BITS;
const INDEX_MASK: usize = (1usize << 9) - 1;

const _: () = assert!(usize::BITS == 64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhysAddr(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VirtAddr(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhysPageNum(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VirtPageNum(usize);

pub fn is_sv39_canonical(raw: usize) -> bool {
    // A：bits[63:39] 必须全部等于 bit38。
    let upper = raw >> 39;
    let bit38 = (raw >> 38) & 1;
    if bit38 == 1 {
        upper == usize::MAX >> 39                
    } else {
        upper == 0
    }
}

fn page_offset(raw: usize) -> usize {
    // B：保留最低的 PAGE_SIZE_BITS 位。
    raw & (PAGE_SIZE - 1)
}

impl PhysAddr {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << PA_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn page_offset(self) -> usize { page_offset(self.0) }
    pub fn floor(self) -> PhysPageNum {
        // C：去掉页内字节偏移。构造函数已经检查过 PA 宽度。
        let res: PhysPageNum = PhysPageNum(self.as_usize() >> 12);

        res
    }
}

impl VirtAddr {
    pub fn new(raw: usize) -> Option<Self> {
        if is_sv39_canonical(raw) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn page_offset(self) -> usize { page_offset(self.0) }
    pub fn floor(self) -> VirtPageNum {
        // D：先排除符号扩展位，再去掉页内偏移，得到 27 位 VPN。
        VirtPageNum((self.as_usize() & VA_MASK) >> PAGE_SIZE_BITS)
    }
}

impl PhysPageNum {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << PPN_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn to_addr(self) -> PhysAddr { PhysAddr(self.0 << PAGE_SIZE_BITS) }
}

impl VirtPageNum {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << VPN_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn to_addr(self) -> VirtAddr {
        // E：补零页内偏移，再按 bit38 做符号扩展，得到完整 VA。
        let page_start = self.as_usize() << PAGE_SIZE_BITS;
        let raw = if (page_start >> 38) & 1 == 1 {
            page_start | !VA_MASK
        } else {
            page_start
        };

        VirtAddr(raw)
    }
    pub fn indexes(self) -> [usize; 3] {
        // F：最高九位在前：[VPN[2], VPN[1], VPN[0]]。
        [
            (self.as_usize() >> 18) & INDEX_MASK,   // VPN[2]
            (self.as_usize() >> 9)  & INDEX_MASK,   // VPN[1]
            (self.as_usize() >> 0)  & INDEX_MASK,   // VPN[0]
        ]
    }
}

pub fn run_demo() {
    let va = VirtAddr::new(0x8040_1234).expect("valid demo VA");
    let vpn = va.floor();
    let pa = PhysAddr::new(0x8123_4234).expect("valid demo PA");
    let ppn = pa.floor();
    assert_eq!(PAGE_SIZE, 4096);
    assert_eq!(vpn.as_usize(), 0x80401);
    assert_eq!(va.page_offset(), 0x234);
    assert_eq!(vpn.indexes(), [2, 2, 1]);
    assert_eq!(vpn.to_addr().as_usize(), 0x8040_1000);
    assert_eq!(ppn.as_usize(), 0x81234);
    assert_eq!(pa.page_offset(), 0x234);
    assert_eq!(ppn.to_addr().as_usize(), 0x8123_4000);
    crate::println!("[addr] va={:#x} vpn={:#x} indexes={:?} offset={:#x}",
        va.as_usize(), vpn.as_usize(), vpn.indexes(), va.page_offset());

    let high = VirtAddr::new(0xffff_ffc0_0000_1234).expect("valid high VA");
    assert_eq!(high.floor().as_usize(), 0x4000001);
    assert_eq!(high.floor().indexes(), [256, 0, 1]);
    assert_eq!(high.page_offset(), 0x234);
    assert_eq!(high.floor().to_addr().as_usize(), 0xffff_ffc0_0000_1000);
    crate::println!("[addr] high={:#x} page_start={:#x}",
        high.as_usize(), high.floor().to_addr().as_usize());

    assert_eq!(VirtPageNum::new(vpn.as_usize()), Some(vpn));
    assert_eq!(PhysPageNum::new(ppn.as_usize()), Some(ppn));
    assert!(PhysAddr::new(1usize << PA_BITS).is_none());
    assert!(PhysPageNum::new(1usize << PPN_BITS).is_none());
    assert!(VirtPageNum::new(1usize << VPN_BITS).is_none());
    assert!(VirtAddr::new(0x0000_0040_0000_0000).is_none());
    crate::println!("[addr] invalid Sv39 address rejected");
}
