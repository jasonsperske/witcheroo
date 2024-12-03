#include <stdio.h>
#include <dos.h>
#include <conio.h>

typedef int bool;
#define TRUE 1
#define FALSE 0

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_READ_CONFIG 0x0A
#define PCI_WRITE_CONFIG 0x0B
#define PCI_BASE_ADDR_0 0x10
#define PCI_BASE_ADDR_1 0x14
#define PCI_FUNCTION_ID 0xB1

/* 3dfx Voodoo2 identifiers */
#define VOODOO2_VENDOR_ID 0x121A
#define VOODOO2_DEVICE_ID 0x0002

/* Voodoo2 Memory Maps */
#define VOODOO2_BASE_ADDR 0x10
#define VOODOO2_FBI_INIT0 0x210
#define VOODOO2_FBI_INIT1 0x214
#define VOODOO2_FBI_INIT2 0x218
#define VOODOO2_FBI_INIT3 0x21C
#define VOODOO2_FBI_INIT4 0x200
#define FBI_OFFSET 0x400000

/* Memory Test patterns */
#define PATTERN1 0x55555555
#define PATTERN2 0xAAAAAAAA
#define PATTERN3 0x00000000
#define PATTERN4 0xFFFFFFFF

void outportl(unsigned long port, unsigned long val)
{
  asm {
    db 0x66
    mov ax, word ptr val
    mov dx, word ptr port
    db 0x66
    out dx, ax
  }
}

unsigned long inportl(unsigned long port)
{
  unsigned long result;
  asm {
    mov dx, word ptr port
    in ax, dx
    mov word ptr result, ax
    inc dx
    inc dx
    in ax, dx
    mov word ptr result+2, ax
  }
  return result;
}

void pci_config_write(unsigned char bus,
                      unsigned char device,
                      unsigned char func,
                      unsigned long offset,
                      unsigned long value)
{

  unsigned long lbus = (unsigned long)bus;
  unsigned long ldevice = (unsigned long)device;
  unsigned long lfunc = (unsigned long)func;

  unsigned long address = (unsigned long)(0x80000000UL |
                                          (lbus << 16) |
                                          (ldevice << 11) |
                                          (lfunc << 8) |
                                          (offset & 0xFC));

  outportl(PCI_CONFIG_ADDRESS, address);
  outportl(PCI_CONFIG_DATA, value);
}

unsigned long pci_config_read_long(unsigned char bus,
                                   unsigned char device,
                                   unsigned char func,
                                   unsigned long offset)
{
  unsigned long lbus = (unsigned long)bus;
  unsigned long ldevice = (unsigned long)device;
  unsigned long lfunc = (unsigned long)func;

  unsigned long address = (unsigned long)(0x80000000UL |
                                          (lbus << 16) |
                                          (ldevice << 11) |
                                          (lfunc << 8) |
                                          (offset & 0xFC));

  outportl(PCI_CONFIG_ADDRESS, address);
  return inportl(PCI_CONFIG_DATA);
}

unsigned pci_config_read(unsigned char bus,
                         unsigned char device,
                         unsigned char func,
                         unsigned char offset)
{
  unsigned long val = pci_config_read_long(bus, device, func, offset);
  if (offset & 2)
  {
    return (unsigned)(val >> 16);
  }
  else
  {
    return (unsigned)val;
  }
}

unsigned char pci_config_read_byte(unsigned char bus,
                                   unsigned char device,
                                   unsigned char func,
                                   unsigned char offset)
{
  unsigned long val = pci_config_read_long(bus, device, func, offset);

  switch (offset & 3)
  {
  case 3:
    return (unsigned char)(val >> 24);
  case 2:
    return (unsigned char)(val >> 16);
  case 1:
    return (unsigned char)(val >> 8);
  case 0:
    return (unsigned char)(val);
  default:
    return 0; /* to silence the compiler warning... */
  }
}

/* Function to check if device is a voodoo2 */
bool is_voodoo2(unsigned char bus,
                unsigned char device,
                unsigned char func)
{
  unsigned short vendor = pci_config_read(bus, device, func, PCI_VENDOR_ID);
  return (bool)(vendor == VOODOO2_VENDOR_ID);
}

void write_voodoo2_reg(unsigned long base_addr,
                       unsigned long reg,
                       unsigned long value)
{
  outportl((base_addr + reg), value);
}

unsigned long read_voodoo2_reg(unsigned long base_addr,
                               unsigned long reg)
{
  return inportl(base_addr + reg);
}

void write_bAND_voodoo2_reg(unsigned long base_addr,
                            unsigned long reg,
                            unsigned long mask)
{
  write_voodoo2_reg(base_addr,
                    reg,
                    read_voodoo2_reg(base_addr, reg) & mask);
}

void write_bOR_voodoo2_reg(unsigned long base_addr,
                           unsigned long reg,
                           unsigned long mask)
{
  write_voodoo2_reg(base_addr,
                    reg,
                    read_voodoo2_reg(base_addr, reg) | mask);
}

unsigned long detect_framebuffer_size(unsigned long base_addr)
{
  unsigned long test_patterns[] = {PATTERN1, PATTERN2, PATTERN3, PATTERN4};
  unsigned long readback;
  int pattern_idx;
  unsigned long mem_tests[] = {0x3FFFF8, 0x7FFFF8, 0xBFFFF8, 0xFFFFF8};
  unsigned long mem_test_sizes[] = {4, 8, 12, 16};
  const char *mem_test_names[] = {"4MB", "8MB", "12MB", "16MB"};

  printf("\nDetecting frame buffer size:\n");

  for (int i = 0; i < 4; i++)
  {
    printf("Testing for %s...\n", mem_test_names[i]);
    for (pattern_idx = 0; pattern_idx < 4; pattern_idx++)
    {
      write_voodoo2_reg(base_addr + FBI_OFFSET, mem_tests[i], test_patterns[pattern_idx]);

      /* Small delay to ensure memory settels */
      for (volatile int j = 0; j < 1000; j++)

        readback = read_voodoo2_reg(base_addr + FBI_OFFSET, mem_tests[i]);

      /* Verify base pattern wasn't corrupted (would indicate wrap) */
      if (readback != test_patterns[pattern_idx])
      {
        printf("%08lX != %08lX\n", test_patterns[pattern_idx], readback);
        printf("Memory wraps at %s - Found previous size\n", mem_test_names[i]);
        return (i > 0) ? mem_test_sizes[i - 1] * 1024 * 1024 : 0;
      }

      readback = read_voodoo2_reg(base_addr + FBI_OFFSET, 0x0);
      if (readback != test_patterns[0])
      {
        printf("Memory wrapped back to base - Found previous size\n");
        return (i > 0) ? mem_test_sizes[i - 1] * 1024 * 1024 : 0;
      }
    }
  }

  printf("All memory tests passed up to %s\n", mem_test_names[3]);
  return mem_test_sizes[3] * 1024 * 1024;
}

void test_voodoo2(unsigned long base_addr)
{
  printf("Initializing Voodoo2...\n");
  // write_voodoo2_reg(base_addr + FBI_OFFSET, VOODOO2_FBI_INIT1, 0x00000001);

  /* Small delay to ensure memory settels */
  // for (volatile int j = 0; j < 1000; j++)

  write_bOR_voodoo2_reg(base_addr, VOODOO2_FBI_INIT1, 0x100);
  write_bOR_voodoo2_reg(base_addr, VOODOO2_FBI_INIT0, 6);
  write_bAND_voodoo2_reg(base_addr, VOODOO2_FBI_INIT2, ~(1 << 22));

  unsigned long fb_size = detect_framebuffer_size(base_addr);
}

void main()
{
  unsigned char bus, device;
  int devices_found = 0;
  unsigned char func = 0;
  unsigned long base_addr, virt_addr, pci_data;

  printf("Witcheroo v0.0\n");

  printf("scanning PCI devices\n");
  printf("====================\n");

  /* Scan all buses and devices */
  for (bus = 0; bus < 255; bus++)
  {
    for (device = 0; device < 32; device++)
    {
      if (is_voodoo2(bus, device, func))
      {
        printf("Found a Voodoo2 card card on bus:%04X slot:%02X\n", bus, device);

        pci_data = pci_config_read_long(bus, device, func, PCI_COMMAND);
        pci_data |= 0x0006; /* sets bits 1 (memory space) and 2 (Bus master) */
        pci_config_write(bus, device, func, PCI_COMMAND, pci_data);

        base_addr = pci_config_read_long(bus, device, func, VOODOO2_BASE_ADDR);
        printf("Base address: %08lx\n", base_addr);
        test_voodoo2(base_addr);
        devices_found++;
      }
    }
  }
  printf("\nTotal Voodoo 2 devices found: %d\n", devices_found);
  printf("\nPress any key to exit...");
  getch();
}