// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

void mon_print_frame_descr(uint32_t*);
void mon_print_frame(uint32_t*, uint32_t*);
void mon_print_symbols(uint32_t*);


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
  { "backtrace", "Display stack backtrace", mon_backtrace },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
  // Capture stack frame base pointer for the frame for this function
  uint32_t *base_ptr = (uint32_t*) read_ebp();

  // Looping over stack frames
  while ((int)base_ptr != 0)
  {
    uint32_t *prev_base = (uint32_t*) *base_ptr;
    mon_print_frame_descr(base_ptr);

    base_ptr = prev_base;
  }

	return 0;
}

void
mon_print_frame(uint32_t *base_ptr, uint32_t *ret_ptr)
{
  // arg_list: Pointer to topmost argument pushed by calling function
  uint32_t *arg_list = base_ptr + 2;

  // Print current frame base pointer nd return point to calling function
  cprintf("ebp %08x eip %08x args ", base_ptr, ret_ptr);

  // Looping over the args in the current frame
  // We don't know how many args there are, so grab five
  int i;
  for (i = 0; i < 5; i++, arg_list++)
  {
    cprintf("%08x ", *arg_list);
  }
  cprintf("\n");

  return;
}

void
mon_print_symbols(uint32_t *ret_ptr)
{
  // Allocate an Eipdebuginfo struct on the stack and get a pointer to it
  struct Eipdebuginfo info;
  struct Eipdebuginfo *info_ptr = &info;

  // Populate the info struct
  debuginfo_eip((uintptr_t)ret_ptr, info_ptr);

  // `Filename:line_number Function_name+offset` where offset is in bytes
  char *format_str = "%s:%d: %.*s+%d\n";

  const char *filename = info_ptr->eip_file;
  int line_num = info_ptr->eip_line;
  int name_length = info_ptr->eip_fn_namelen;
  const char *func_name = info_ptr->eip_fn_name;

  // Offset of return point from the function prolog
  int offset = (int)ret_ptr - info_ptr->eip_fn_addr;

  cprintf(format_str, filename, line_num, name_length, func_name, offset);

  return;
}

void
mon_print_frame_descr(uint32_t *base_ptr)
{
  // Return pointer to previous stack frame
  uint32_t *ret_ptr = (uint32_t*) *(base_ptr + 1);

  mon_print_frame(base_ptr, ret_ptr);
  mon_print_symbols(ret_ptr);

  return;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
