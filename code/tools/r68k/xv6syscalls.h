// xv6syscalls.c
void set_emulator_root(char *dirname);
uint64_t do_xv6syscall(int op, int *islonglong);
