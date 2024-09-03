#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "main.h"

extern uint32_t start_address;

// The following are the XV6 defines, structs etc. that
// need to be mapped to the host system

// param.h bits
#define MAXARGS	     100	// Maximum # command-line arguments

// fcntl.h bits
#define XO_CREAT  0x200

// fstat.h bits
#define XT_DIR  1		// Directory
#define XT_FILE 2		// File
#define XT_DEV  3		// Device

struct xvstat {
  int16_t type;			// Type of file
  int32_t dev;			// File system's disk device
  uint32_t ino;			// Inode number
  uint16_t nlink;		// Number of links to file
  uint32_t size;		// Size of file in bytes
};

// Get the stack pointer value.
// Ensure it's in the RAM range.
static uint32_t get_sp(void) {
  return (m68ki_cpu.dar[15] & (RAM_SIZE - 1));
}

// Get a pointer to a location in the emulator's memory
// given the location's address. Return NULL is the
// emulator's address is zero.
static uint8_t *get_memptr(uint32_t addr) {
  if (addr == 0)
    return (NULL);
  addr &= RAM_SIZE - 1;		// Keep in array bounds
  // printf("get_memptr 0x%x is 0x%p\n", addr, &g_ram[addr]);
  return (&g_ram[addr]);
}

// Functions to read/write (un)signed char/int arguments at the
// "off"set on the stack. Offset 0 is the 1st byte of the 1st argument.

// Signed 8-bit char argument. All arguments are pushed
// as 32-bit ints, so we do 12+3 to get to the actual byte.
static int8_t scarg(int off) {
  uint32_t sp = get_sp() + 15 + off;
  int8_t val = cpu_read_byte(sp);
  // printf("scarg %d is %d\n", off, val);
  return (val);
}

// Unsigned 32-bit integer argument
uint32_t uiarg(int off) {
  uint32_t sp = get_sp() + 12 + off;
  uint32_t val = cpu_read_long(sp);
  // printf("uiarg %d is %d 0x%x\n", off, val, val);
  return (val);
}

// Signed 32-bit integer argument
int32_t siarg(int off) {
  uint32_t sp = get_sp() + 12 + off;
  int32_t val = (int32_t) cpu_read_long(sp);
  // printf("siarg %d is %d\n", off, val);
  return (val);
}


				/* The following two buffers are used as */
				/* part of the translation from virtal */
				/* absolute filenames to native ones. We */
				/* only have 2 buffers, so if you call */
				/* xlate_filename() 3 times, the 1st return */
				/* value will be destroyed. */
static char realfilename[2][2 * PATH_MAX];
static char *rfn[2];
static int whichrfn = 0;

/* Translate from a filename to one which is possibly rooted in $XV6ROOT.
 * Note we return a pointer to one of two buffers. The caller does not
 * have to free the returned pointer, but successive calls will destroy
 * calls from >2 calls earlier.
 */
static char *xlate_filename(char *name) {
  int i = whichrfn;

  if (name == NULL)
    return (NULL);
  if (name[0] != '/')
    return (name);		/* Relative, keep it relative */
  strcpy(rfn[i], name);		/* Copy name into buffer */
  whichrfn = 1 - whichrfn;	/* Switch to other buffer next time */
  // printf("xlate filename %s to %s\n", name, realfilename[i]);
  return (realfilename[i]);
}

void set_emulator_root(char *dirname) {
  // printf("Setting emulator root to %s\n", dirname);
  // Test if the dirname exists if not ""
  if (strlen(dirname) != 0) {
    DIR *D = opendir(dirname);
    if (D == NULL) {
      fprintf(stderr, "Unable to use XV6ROOT %s: %s\n",
	      dirname, strerror(errno));
      exit(1);
    }
    closedir(D);
  }
  strcpy(realfilename[0], dirname);
  strcpy(realfilename[1], dirname);
  rfn[0] = realfilename[0];
  rfn[0] += strlen(realfilename[0]);
  rfn[1] = realfilename[1];
  rfn[1] += strlen(realfilename[1]);
}

// Copy a native stat struct to an xv6 stat struct
void copystat(struct stat *hstat, struct xvstat *xstat) {
  xstat->type = XT_FILE;
  if ((hstat->st_mode & S_IFMT) == S_IFDIR)
    xstat->type = XT_DIR;
  if ((hstat->st_mode & S_IFMT) == S_IFBLK)
    xstat->type = XT_DEV;
  if ((hstat->st_mode & S_IFMT) == S_IFCHR)
    xstat->type = XT_DEV;

  xstat->dev = hstat->st_dev;
  xstat->ino = hstat->st_ino;
  xstat->nlink = hstat->st_nlink;
  xstat->size = hstat->st_size;
}

// Given an address in RAM, get the
// pointer value at that address
static uint32_t deref_ptr(uint32_t addr) {
  uint32_t val;

  if (addr == 0)
    return (0);
  addr &= RAM_SIZE - 1;		// Keep in array bounds
  val = cpu_read_long(addr);
  // printf("deref_ptr 0x%x is 0x%x\n", addr, val);
  return (val);
}

// Spawn a new running program.
// argc is a count, argv is
// the address in RAM of the array.
void spawn(uint32_t argc, uint32_t argv) {
  int i, cnt, fd;

  // Guest means a value that exists in
  // the emulated guest program, not a
  // value in the emulator
  uint32_t guest_argv[MAXARGS];
  uint32_t guest_ptr;
  uint32_t guest_src;
  uint32_t guest_dst;
  char *ptr;

  // If there are no arguments, or too many, fail.
  // Yes a goto, sorry!
  if (argc < 1 || argc > MAXARGS)
    goto spawnfail;


  // Check that all the argv's are valid
  guest_ptr = argv;
  for (i = 0; i < (int) argc; i++) {
    if (get_memptr(guest_ptr) == NULL)
      goto spawnfail;
    guest_ptr += 4;		// 4 is sizeof(char *)
  }

  // Try to open the program
  fd = open(xlate_filename((char *) get_memptr(deref_ptr(argv))), O_RDONLY);
  if (fd == -1)
    goto spawnfail;

  // Copy each argument on to the stack
  // and save the destination pointer value
  guest_dst = RAM_SIZE;
  for (i = argc - 1; i >= 0; i--) {
    // Count the strings's length and the NUL.
    guest_src = argv + 4 * i;	// 4 is sizeof(char *)
    cnt = strlen((const char *) get_memptr(deref_ptr(guest_src))) + 1;
    guest_dst -= cnt;
    memmove(get_memptr(guest_dst), get_memptr(deref_ptr(guest_src)), cnt);
    guest_argv[i] = htobe32(guest_dst);
  }

  // Put a NULL on the stack as the last
  // element in the argv array
  guest_dst -= 4;		// 4 is sizeof(char *)
  memset(get_memptr(guest_dst), 0, 4);

  // Write the argv pointers on the stack
  for (i = argc - 1; i >= 0; i--) {
    guest_dst -= 4;		// 4 is sizeof(char *)
    memmove(get_memptr(guest_dst), &(guest_argv[i]), 4);
  }

  // Put argv, the pointer to the base of the array,
  // on the stack. Yes, we point to address 4 bytes
  // above us :-)
  guest_src = htobe32(guest_dst);
  guest_dst -= 4;
  memmove(get_memptr(guest_dst), &guest_src, 4);

  // Finally put the argc on the stack
  guest_src = htobe32(argc);
  guest_dst -= 4;
  memmove(get_memptr(guest_dst), &guest_src, 4);

  // Load the program into RAM at the start_addr.
  // Do this after we move the arguments as we
  // might overwrite them
  ptr = (char *) &(g_ram[start_address]);
  while (1) {
    cnt = read(fd, ptr, 512);
    if (cnt <= 0) break;
    ptr += cnt;
  }
  close(fd);

  // Set the new PC and SP
  m68ki_cpu.pc = start_address;
  m68ki_cpu.dar[15] = guest_dst;
  return;

spawnfail:
  fprintf(stderr, "xv6 spawn failed\n");
  exit(1);
}


uint64_t do_xv6syscall(int op, int *islonglong) {
  int64_t result;		// Native syscall result
  uint8_t ch;			// Character to print out
  const char *path;		// Pointer to pathname
  const char *newpath;		// Pointer to new pathname
  mode_t mode;			// File mode
  int oflags, flags;		// File flags: emulated and host
  int fd;			// File descriptor
  size_t cnt;			// Count in bytes
  uint8_t *buf;			// Pointer to buffer
  struct stat hstat;		// Host stat struct;
  struct xvstat *xstat;		// XV6 stat struct;
  uint32_t argc, argv;		// argc and argv from spawn
  int offset, whence;		// lseek arguments

  errno = 0;			// Start with no syscall errors
  *islonglong = 0;		// Assume a 32-bit result

  switch (op) {
  case 0:			// consputc
    ch = scarg(0);
    putchar(ch);
    fflush(stdout);
    result = 0;
    break;
  case 1:			// _exit
    exit(siarg(0));
  case 3:			// read
    fd = uiarg(0);
    buf = get_memptr(uiarg(4));
    if (buf == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    cnt = uiarg(8);
    result = read(fd, buf, cnt);
    break;
  case 4:			// write
    fd = uiarg(0);
    buf = get_memptr(uiarg(4));
    if (buf == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    cnt = uiarg(8);
    result = write(fd, buf, cnt);
    break;
  case 5:			// open
    path = (const char *) xlate_filename((char *) get_memptr(uiarg(0)));
    if (path == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    oflags = uiarg(4);
    // XV6 doesn't have modes - not yet
    //mode= uiarg(8);
    mode = 0644;

    // Map the xv6 flags to the host system.
    // Keep the lowest two bits.
    flags = oflags & 0x3;
    flags |= (oflags & XO_CREAT) ? O_CREAT : 0;
    result = open(path, flags, mode);
    break;
  case 6:			// close
    fd = uiarg(0);
    result = close(fd);
    break;
  case 9:			// link
    path = (const char *) xlate_filename((char *) get_memptr(uiarg(0)));
    if (path == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    newpath = (const char *) xlate_filename((char *) get_memptr(uiarg(4)));
    if (newpath == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    result = link(path, newpath);
    break;
  case 10:			// unlink
    path = (const char *) xlate_filename((char *) get_memptr(uiarg(0)));
    if (path == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }

    // XV6 doesn't have rmdir() yet. So if this is a directory, we
    // do an rmdir() instead of the unlink().
    result = stat(path, &hstat);
    if (result == -1)
      break;

    if ((hstat.st_mode & S_IFMT) == S_IFDIR)
      result = rmdir(path);
    else
      result = unlink(path);
    break;
  case 12:			// chdir
    path = (const char *) xlate_filename((char *) get_memptr(uiarg(0)));
    if (path == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    result = chdir(path);
    break;
  case 13:			// fstat
    fd = uiarg(0);
    xstat = (struct xvstat *) get_memptr(uiarg(4));
    if (xstat == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    result = fstat(fd, &hstat);
    if (result == -1)
      break;
    copystat(&hstat, xstat);
    break;
  case 14:			// dup
    fd = uiarg(0);
    result = dup(fd);
    break;
  case 15:			// mkdir
    path = (const char *) xlate_filename((char *) get_memptr(uiarg(0)));
    if (path == NULL) {
      result = -1;
      errno = EFAULT;
      break;
    }
    // mode= uiarg(4);
    mode = 0755;
    result = mkdir(path, mode);
    break;
  case 16:			// spawn
    argc = uiarg(0);
    argv = uiarg(4);
    spawn(argc, argv);
    break;
  case 17:			// consgetc
    ch = 0;
    while (ch == 0) {
      read(STDIN_FILENO, &ch, 1);
    }
    result= ch;
    break;
  case 18:			// lseek
    fd = uiarg(0);
    offset= siarg(4);
    whence= siarg(8);
    result= lseek(fd, offset, whence);
    break;

  default:
    fprintf(stderr, "Unhandled xv6 syscall %d\n", op);
    exit(1);
  }

  return (result);
}
