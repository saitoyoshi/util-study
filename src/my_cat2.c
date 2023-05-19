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
static bool cat(char *inbuf, size_t insize, char *outbuf, size_t outsize, bool show_nonprinting, bool show_tabs, bool number, bool number_nonblank, bool show_ends, bool squeeze_blank) {
  /* このコードでは、テキストファイルのデータをバッファを通じて読み込み、一部の処理を行ってから出力する機能が実装されています。各変数の目的は以下の通りです。

- `inbuf`と`outbuf`は、それぞれ入力データと出力データを格納するためのバッファ（大きな文字列の配列）を指すポインタです。`inbuf`はファイルから読み込んだデータを保持し、`outbuf`は処理後のデータを保持します。

- `bpin`と`bpout`は、それぞれ`inbuf`と`outbuf`内で現在処理している位置を指すポインタです。`bpin`は読み込みの現在位置を示し、`bpout`は書き込みの現在位置を示します。

- `eob`（End Of Buffer）は、入力バッファの終端を指すポインタです。これは新たなデータを読み込むかどうかを判断するために使用されます。

- `newlines`は、読み込んだデータ内の改行数をカウントするための変数です。

それぞれの役割を理解するためには、この`cat`関数の全体的な動作を把握することが重要です。基本的には、ファイルからデータを読み込んで（`inbuf`に格納して）から、それを一定の処理を行って（行番号をつける、タブを表示するなど）、出力バッファ`outbuf`に格納し、最終的にそのバッファの内容を出力します。その間、`bpin`と`bpout`はそれぞれ読み込みと書き込みの進行度を追跡するために使用されます。*/
  unsigned char ch;
  char *bpin;
  char *eob;
  char *bpout;
  size_t n_read;
  int newlines = newlines2;

  eob = inbuf;
  bpin = eob + 1;
  bpout = outbuf;

  while (true) {
    do {
      if (outbuf + outsize <= bpout) {
        // 出力バッファがいっぱいならば
        char *wp = outbuf;
        size_t remaining_bytes;
        do {
          if (write(STDOUT_FILENO, wp, outsize) != outsize) {
            fprintf(stderr, "write error in cat()\n");
          }
          wp += outsize;
          remaining_bytes = bpout - wp;
        } while (outsize <= remaining_bytes);
        memmove(outbuf, wp, remaining_bytes);
        bpout = outbuf + remaining_bytes;
      }
      if (bpin > eob) {
        // 入力バッファが空
        bool input_pending = false;
        if (!input_pending) {
          write_pending(outbuf, &bpout);
        }
        n_read = read(input_desc, inbuf, insize);
        if (n_read == -1) {
          write_pending(outbuf, &bpout);
          newlines2 = newlines;
          return false;
        }
        if (n_read == 0) {
          write_pending(outbuf, &bpout);
          newlines2 = newlines;
          return true;
        }
        bpin = inbuf;
        eob = bpin + n_read;
        *eob = '\n';
      } else {
        if (++newlines > 0) {
          if (newlines >= 2) {
            newlines = 2;
            if (squeeze_blank) {
              ch = *bpin++;
              continue;
            }
          }
          if (number && !number_nonblank) {
            next_line_num();
            bpout = stpcpy(bpout, line_num_print);
          }
        }
        if (show_ends) {
          *bpout++ = '$';
        }
        *bpout++ = '\n';
      }
      ch = *bpin++;
    } while (ch == '\n');

    if (newlines >= 0 && number) {
      next_line_num();
      bpout = stpcpy(bpout, line_num_print);
    }

    if (show_nonprinting) {
      while (true) {
        if (ch >= 32) {
          if (ch < 127) {
            *bpout++ = ch;
          } else if (ch == 127) {
            *bpout++ = '^';
            *bpout++ = '?';
          } else {
            *bpout++ = 'M';
            *bpout++ = '-';
            if (ch >= 128 + 32) {
              if (ch < 128 + 127) {
                *bpout++ = ch - 128;
              } else {
                *bpout++ = '^';
                *bpout++ = '?';
              }
            } else {
              *bpout++ = '^';
              *bpout++ = ch - 128 + 64;
            }
          }
        } else if (ch == '\t' && !show_tabs) {
          *bpout++ = '\t';
        } else if (ch == '\n') {
          newlines = -1;
          break;
        } else {
          *bpout++ = '^';
          *bpout++ = ch + 64;
        }
        ch = *bpin++;
      }
    } else {
      while (true) {
        if (ch == '\t' && show_tabs) {
          *bpout++ = '^';
          *bpout++ = ch + 64;
        } else if (ch != '\n') {
          *bpout++ = ch;
        } else {
          newlines = -1;
          break;
        }

        ch = *bpin++;
      }
    }
  }
}
int main(int argc, char *argv[]) {
  // char inbuf[1024];
  char inbuf[1024];
  size_t insize = 64;
  char outbuf[1024];
  size_t outsize = 64;
  bool show_nonprinting = false;
  bool show_tabs = true;
  bool number = true;
  bool number_nonblank = true;
  bool show_ends = true;
  bool squeeze_blank = true;
  input_desc = open(argv[1], O_RDONLY);
  cat(inbuf,insize,outbuf,outsize,show_nonprinting,show_tabs,number,number_nonblank, show_ends, squeeze_blank);
  // char str[] = "hello world!";
  // int n = sizeof(str) / sizeof(str[0]);
  // char *bpout = str + 3;
  // printf("%d\n", n);
  // write_pending(str, &bpout);
  // usage(0, "cat");
  // char i[] = "hello world!";
  // bool b = simple_cat(i,1);
  // printf("%d\n", (int)b);
}
