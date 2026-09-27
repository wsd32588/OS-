# Host-side tests

`make test` builds these tests with the host compiler and runs them outside
QEMU. The PMM test provides a page-aligned host virtual-memory region and then
executes deterministic allocation and release operations against it.

Future Sv39 page-table code should keep pure page-table construction, walking,
mapping, unmapping, and permission checks separate from RISC-V CSR operations.
The pure logic can then run here for thousands of deterministic operations.
Writing `satp`, executing `sfence.vma`, and checking real page faults still
belong in QEMU integration tests.
