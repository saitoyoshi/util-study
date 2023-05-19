#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define EXIT_SUCCESS 0
#define DEV_BSIZE 512

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define STREQ(a,b) (strcmp(a,b) == 0)
#define ST_BLKSIZE(statbuf) ((0 < (statbuf).st_blksize \
&& (statbuf).st_blksize <= ((size_t)-1) / 8 + 1) \
? (statbuf).st_blksize : DEV_BSIZE)
enum { IO_BUFSIZE = 128*1024 };

static inline size_t io_blksize(struct stat sb) {
  return MAX(IO_BUFSIZE, ST_BLKSIZE(sb));
}

#define PROGRAM_NAME "my_cat2"
#define AUTHORS proper_name("yoshi")

static char const *infile;
static int input_desc;
#define LINE_COUNTER_BUF_LEN 20
static char line_buf[LINE_COUNTER_BUF_LEN] = {
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', '0', '\t', '\0',
};
static char *line_num_print = line_buf + LINE_COUNTER_BUF_LEN - 8;
static char *line_num_start = line_buf +
LINE_COUNTER_BUF_LEN - 3;
static char *line_num_end = line_buf +
LINE_COUNTER_BUF_LEN - 3;

static int newlines2 = 0;

static inline void *ptr_align(void const *ptr, size_t alignment) {
  char const *p0 = ptr;
  char const *p1 = p0 + alignment - 1;
  return (void *) (p1 - (size_t) p1 % alignment);
}

void usage(int status, char *program_name) {
  if (status != EXIT_SUCCESS) {
    puts("emit_try_help()");
  } else {
    printf("\
    Usage: %s [option]... [file]... \n",
    program_name);
    fputs("\
    ファイルを結合して、標準出力に出す\n", stdout);
    fputs("\n\
    -A, --show-all           -vETと同様\n\
    -b, --number-nonblank    空行ではない行に行番号を表示する -n を上書きする\n\
    -e                        -vEと同様\n\
    -E, --show-ends           各行の終わりに$を表示する\n\
    -n, --number              すべての出力に行番号をつける\n\
    -s, --squeeze-blank       連続する空行を出力しない\n", stdout);
    fputs("\
    -t                        -vTと同様\n\
    -TCFLSH                   Tab文字を^Iとして表示する\n\
    -u                        無視されるオプション\n\
    -v, --show-nonprinting    ^とM-という記法をつかって表示できない文字を置き換えて出力する\n\
    ", stdout);
    printf("\n\
    %s f - g fの内容を出力し、それから標準入力とgの内容を読む\n\
    %s 標準入力を標準出力にコピーする\n", program_name, program_name);
    exit(status);
  }
}

static void next_line_num(void) {
  char *endp = line_num_end;
  do {
    if ((*endp)++ < '9') {
      return;
    }
    *endp-- = '0';
  } while (endp >= line_num_start);
  if (line_num_start > line_buf) {
    *--line_num_start = '1';
  } else {
    *line_buf = '>';
  }
  if (line_num_start < line_num_print) {
    line_num_print--;
  }
}
static bool simple_cat(char *buf, size_t bufsize) {
  size_t n_read;

  while (true) {
    n_read = read(input_desc, buf, bufsize);
    if (n_read == -1) {
      fprintf(stderr, "read error in simple_cat()\n");
      return false;
    }
    if (n_read == 0) {
      return true;
    }
    {
      size_t n = n_read;
      if (write(STDOUT_FILENO, buf, n) != n) {
        fprintf(stderr, "write error in simple_cat()\n");
      }
    }
  }
}
static inline void write_pending(char *outbuf, char **bpout) {
  size_t n_write = *bpout - outbuf;
  if (0 < n_write) {
    if (write(STDOUT_FILENO, outbuf, n_write) != n_write) {
      fprintf(stderr, "write error in write_pending\n");
    }
    *bpout = outbuf;
  }
}
int main(void) {
  char str[] = "hello world!";
  int n = sizeof(str) / sizeof(str[0]);
  char *bpout = str + 3;
  // printf("%d\n", n);
  write_pending(str, &bpout);
  // usage(0, "cat");
  // char i[] = "hello world!";
  // bool b = simple_cat(i,1);
  // printf("%d\n", (int)b);
}
