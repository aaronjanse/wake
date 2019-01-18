/* Wake vfork exec shim */
#include <sys/errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "blake2.h"

// Can increase to 64 if needed
#define HASH_BYTES 32

static int do_hash_dir() {
  for (int i = 0; i < HASH_BYTES; ++i)
    printf("00");
  printf("\n");
  return 0;
}

static int do_hash(const char *file) {
  struct stat stat;
  int fd, got;
  uint8_t hash[HASH_BYTES], buffer[8192];
  blake2b_state S;

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    if (errno == EISDIR) return do_hash_dir();
    perror("open");
    return 1;
  }

  if (fstat(fd, &stat) != 0) {
    if (errno == EISDIR) return do_hash_dir();
    perror("fstat");
    return 1;
  }

  if (S_ISDIR(stat.st_mode)) return do_hash_dir();

  blake2b_init(&S, sizeof(hash));
  while ((got = read(fd, &buffer[0], sizeof(buffer))) > 0)
    blake2b_update(&S, &buffer[0], got);
  blake2b_final(&S, &hash[0], sizeof(hash));

  if (got < 0) {
    perror("read");
    return 1;
  }

  for (int i = 0; i < (int)sizeof(hash); ++i)
    printf("%02x", hash[i]);
  printf("\n");

  return 0;
}

int main(int argc, char **argv) {
  int stdin_fd, stdout_fd, stderr_fd;
  const char *root, *dir;

  if (argc < 7) return 1;

  root = argv[4];
  if ((root[0] != '.' || root[1] != 0) && chdir(root)) {
    fprintf(stderr, "chdir: %s: %s\n", root, strerror(errno));
    return 127;
  }

  dir = argv[5];
  if ((dir[0] != '.' || dir[1] != 0) && chdir(dir)) {
    fprintf(stderr, "chdir: %s: %s\n", dir, strerror(errno));
    return 127;
  }

  stdin_fd = open(argv[1], O_RDONLY);
  if (stdin_fd == -1) {
    fprintf(stderr, "open: %s: %s\n", argv[1], strerror(errno));
    return 127;
  }
  dup2(stdin_fd, 0);
  close(stdin_fd);

  stdout_fd = atoi(argv[2]);
  stderr_fd = atoi(argv[3]);
  dup2(stdout_fd, 1);
  dup2(stderr_fd, 2);
  close(stdout_fd);
  close(stderr_fd);

  if (strcmp(argv[6], "<hash>")) {
    execv(argv[6], argv+6);
    fprintf(stderr, "execv: %s: %s\n", argv[6], strerror(errno));
    return 127;
  } else {
    return do_hash(argv[7]);
  }
}
