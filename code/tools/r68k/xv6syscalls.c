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

// The following are the XV6 defines, structs etc. that
// need to be mapped to the host system

// fcntl.h bits
#define XO_CREAT  0x200

// Get the stack pointer value.
// Ensure it's in the RAM range.
static uint32_t get_sp(void) {
  return(m68ki_cpu.dar[15] & (RAM_SIZE-1));
}

// Get a pointer to a location in the emulator's memory
// given the location's address. Return NULL is the
// emulator's address is zero.
static uint8_t *get_memptr(uint32_t addr) {
  if (addr==0) return(NULL);
  addr &= RAM_SIZE-1;	// Keep in array bounds
  // printf("get_memptr 0x%x is 0x%p\n", addr, &g_ram[addr]);
  return(&g_ram[addr]);
}

// Functions to read/write (un)signed char/int arguments at the
// "off"set on the stack. Offset 0 is the 1st byte of the 1st argument.

// Signed 8-bit char argument. All arguments are pushed
// as 32-bit ints, so we do 12+3 to get to the actual byte.
static int8_t scarg(int off) {
  uint32_t sp= get_sp() + 15 + off;
  int8_t val= cpu_read_byte(sp);
  // printf("scarg %d is %d\n", off, val);
  return(val);
}

// Unsigned 32-bit integer argument
uint32_t uiarg(int off) {
  uint32_t sp= get_sp() + 12 + off;
  uint32_t val= cpu_read_long(sp);
  // printf("uiarg %d is %d 0x%x\n", off, val, val);
  return(val);
}


                                /* The following two buffers are used as */
                                /* part of the translation from virtal */
                                /* absolute filenames to native ones. We */
                                /* only have 2 buffers, so if you call */
                                /* xlate_filename() 3 times, the 1st return */
                                /* value will be destroyed. */
static char realfilename[2][2 * PATH_MAX];
static char *rfn[2];
static int whichrfn=0;

/* Translate from a filename to one which is possibly rooted in $XV6ROOT.
 * Note we return a pointer to one of two buffers. The caller does not
 * have to free the returned pointer, but successive calls will destroy
 * calls from >2 calls earlier.
 */
static char *xlate_filename(char *name)
{
    int i=whichrfn;

    if (name == NULL) return (NULL);
    if (name[0] != '/') return (name);  /* Relative, keep it relative */
    strcpy(rfn[i], name);               /* Copy name into buffer */
    whichrfn= 1 - whichrfn;             /* Switch to other buffer next time */
printf("xlate filename %s to %s\n", name, realfilename[i]);
    return (realfilename[i]);
}

void set_emulator_root(char *dirname)
{
printf("Setting emulator root to %s\n", dirname);
  // Test if the dirname exists if not ""
  if (strlen(dirname)!=0) {
    DIR *D= opendir(dirname);
    if (D==NULL) {
      fprintf(stderr, "Unable to use XV6ROOT %s: %s\n",
          dirname, strerror(errno));
      exit(1);
    }
    closedir(D);
  }
  strcpy(realfilename[0], dirname);      
  strcpy(realfilename[1], dirname);      
  rfn[0] = realfilename[0]; rfn[0] += strlen(realfilename[0]);
  rfn[1] = realfilename[1]; rfn[1] += strlen(realfilename[1]);
}


uint64_t do_xv6syscall(int op, int *islonglong) {
  int64_t result;       // Native syscall result
  uint8_t ch;		// Character to print out
  const char *path;	// Pointer to pathname
  const char *newpath;	// Pointer to new pathname
  mode_t mode;          // File mode
  int oflags, flags;    // File flags: emulated and host
  int fd;		// File descriptor
  size_t cnt;           // Count in bytes
  uint8_t *buf;         // Pointer to buffer
  struct stat hstat;    // Host stat struct;

  errno= 0;             // Start with no syscall errors
  *islonglong= 0;       // Assume a 32-bit result

  switch(op) {
    case 0:		// consputc
      ch= scarg(0);
      putchar(ch);
      result= 0;
      break;
    case 3:             // read
      fd= uiarg(0);
      buf= get_memptr(uiarg(4));
      if (buf==NULL) { result=-1; errno=EFAULT; break; }
      cnt= uiarg(8);
      result= read(fd, buf, cnt);
      break;
    case 4:             // write
      fd= uiarg(0);
      buf= get_memptr(uiarg(4));
      if (buf==NULL) { result=-1; errno=EFAULT; break; }
      cnt= uiarg(8);
      result= write(fd, buf, cnt);
      break;
    case 5:             // open
      path= (const char *)xlate_filename((char *)get_memptr(uiarg(0)));
      if (path==NULL) { result=-1; errno=EFAULT; break; }
      oflags= uiarg(4);
      // XV6 doesn't have modes - not yet
      //mode= uiarg(8);
      mode= 0644;

      // Map the xv6 flags to the host system.
      // Keep the lowest two bits.
      flags=   oflags & 0x3;
      flags |= (oflags & XO_CREAT)   ? O_CREAT : 0;
printf("Opening %s flags 0x%x mode 0%o ", path, flags, mode);

      result= open(path, flags, mode);
printf("result %ld\n", result);
      break;
    case 6:             // close
      fd= uiarg(0);
      result= close(fd);
      break;
    case 9:             // link
      path=    (const char *)xlate_filename((char *)get_memptr(uiarg(0)));
      if (path==NULL) { result=-1; errno=EFAULT; break; }
      newpath= (const char *)xlate_filename((char *)get_memptr(uiarg(4)));
      if (newpath==NULL) { result=-1; errno=EFAULT; break; }
      result= link(path, newpath);
      break;
    case 10:            // unlink
      path=    (const char *)xlate_filename((char *)get_memptr(uiarg(0)));
      if (path==NULL) { result=-1; errno=EFAULT; break; }

      // XV6 doesn't have rmdir() yet. So if this is a directory, we
      // do an rmdir() instead of the unlink().
      result= stat(path, &hstat);
      if (result==-1) break;

      if ((hstat.st_mode & S_IFMT)==S_IFDIR)
        result= rmdir(path);
      else
        result= unlink(path);
      break;
    case 12:            // chdir
      path= (const char *)xlate_filename((char *)get_memptr(uiarg(0)));
      if (path==NULL) { result=-1; errno=EFAULT; break; }
printf("chdir to %s\n", path);
      result= chdir(path);
      break;
    case 15:		// mkdir
      path= (const char *)xlate_filename((char *)get_memptr(uiarg(0)));
      if (path==NULL) { result=-1; errno=EFAULT; break; }
      // mode= uiarg(4);
      mode= 0755;
      result= mkdir(path, mode);
      break;

    default: fprintf(stderr, "Unhandled xv6 syscall %d\n", op); exit(1);
  }
  
  return(result);
}
