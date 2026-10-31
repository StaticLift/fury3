/* vita-elf-create appends the module's SCE data (module info, import tables,
 * a few KB) right after the end of the text segment, and fails when the data
 * segment -- which VitaSDK's armelf.x places at the next 64 KB boundary --
 * starts closer than that. Whether it fits depended on where .text happened to
 * end (it failed once ScePower stubs moved the end to 0xA10 below a boundary).
 * Moving the location counter past the next boundary before the data segment
 * is aligned guarantees >= 64 KB of room. No section covers the gap, so it
 * costs address space, not memory. */
SECTIONS
{
  . = ALIGN(0x10000) + 0xF000;
}
INSERT AFTER .eh_frame;
