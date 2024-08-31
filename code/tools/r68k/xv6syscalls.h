// xv6syscalls.c
void set_emulator_root(char *dirname);
// int set_arg_env(uint16_t sp, char **argv, char **envp);
uint64_t do_xv6syscall(int op, int *islonglong);
