#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdbool.h>
#define PROGRAM_NAME "my_echo"
#define STREQ(a, b) (strcmp (a, b) == 0)
#define AUTHORS \
    proper_name("yoshi")
#define EXIT_SUCCESS 0
// もし真ならば、デフォルトでバックスラッシュエスケープを解釈します。
//  X/Open Portability Guideという移植性を向上させるためのガイドラインで、デフォルトがfalseなのでXPG仕様に従わずバックスラッシュエスケープはデフォルトで解釈されません
#ifndef DEFAULT_ECHO_TO_XPG
enum {
    DEFAULT_ECHO_TO_XPG = false
};
#endif

void usage(int status, char *program_name) {
    assert(status == EXIT_SUCCESS);
    printf("Usage: %s [short-option]... [string]...\nor: %s long-option", program_name, program_name);
    fputs("echo the string(s) to starndard output.\n\n\
    -n      do not output the trailing newline\n", stdout);
    fputs(DEFAULT_ECHO_TO_XPG ? "\
    -e      enable interpretation of backslash escapes (default)\n\
    -E      disable interpretation of backslash escapes\n"
    : "\
    -e       enable interpretaion of backslash escapes\n\
    -E          disable interpretation of backslash escapes (default)\n", stdout);
    fputs("\
    \n\
    If -e is in effect, the following sequences are recognized:\n\n", stdout);
    fputs("\
    \\\\    backslash\n\
    \\e     escape\n\
    \\n     new line\n\
    \\t     horizontal tab\n", stdout);
    fputs("\
    \\0NNN  byte with octal value NNN (1 to 3 digits)\n\
    \\xHH   byte with hexdecimal value HH (1 to 2 digits)\n", stdout);
    exit(status);
}

// 「引数cを16進数の文字から整数に変換します。」16進数を１０進数に変換
static int
hex2dec(unsigned char c) {
    switch (c) {
        default:
            return c - '0';
        case 'a': case 'A': return 10;
        case 'b': case 'B': return 11;
        case 'c': case 'C': return 12;
        case 'd': case 'D': return 13;
        case 'e': case 'E': return 14;
        case 'f': case 'F': return 15;
    }
}

// 「リスト内の単語を標準出力に出力します。最初の単語が '-n' の場合、末尾に改行を出力しません。また、Version 9 の Unix システムの echo 構文もサポートしています。」
int main(int argc, char **argv) {
    bool display_return = true;  // このフラグが設定されていれば、改行文字を出力。デフォルトはechoは改行する
    bool posixly_correct = getenv("POSIXLY_CORRECT");
    // オプションを解析するかどうかを制御する。このフラグが設定されていない場合、echoはすべての引数をテキストとして扱う。通常はNULL
    bool allow_options =
        (!posixly_correct || (!DEFAULT_ECHO_TO_XPG && 1 < argc && STREQ(argv[1], "-n")));  // もしPOSIXLY_CORRECTが環境変数として設定されていて、引数があり、最初の引数が-nと等しければ

    // System Vマシン上での既存のシステムシェルスクリプトの互換性を保つための処理を行っている。このフラグはバックスラッシュエスケープ文字の処理に使用されます。このフラグが設定されている場合、echoはバックスラッシュエスケープ文字を解釈する
    bool do_v9 = DEFAULT_ECHO_TO_XPG;  // 通常がここでfalseが代入される


    // 短縮形のオプションを受け入れないために、parse_long_options を使用せずに直接オプションを解析します。echo --help or echo --version が動くようにしている
    if (allow_options && argc == 2) {
        if (STREQ(argv[1], "--help")) {
            usage(0, argv[0]);
        }
        if (STREQ(argv[1], "--version")) {
            puts("v1.0.0");
            return 0;
        }
    }
    // プログラム名を除いたコマンドライン引数に対して処理を行うための準備です。パスパラメタの左シフト的なことをしている
    --argc;
    ++argv;
    // getoptを使わずに直接入力をパースする
    if (allow_options) {
        while (argc > 0 && *argv[0] == '-') {
            char const *temp = argv[0] + 1;
            size_t i;
            for (i = 0; temp[i]; i++) {
                switch (temp[i]) {
                    case 'e': case 'E': case 'n':
                        break;
                    default:
                        goto just_echo;
                }
            }
            if (i == 0) {
                goto just_echo;
            }
            while (*temp) {
                switch (*temp++) {
                    case 'e':
                        do_v9 = true;
                        break;
                    case 'E':
                        do_v9 = false;
                        break;
                    case 'n':
                        display_return = false;
                        break;
                }
            }
            argc--;
            argv++;
        }
    }

just_echo:

    if (do_v9 || posixly_correct) {
        while (argc > 0) {
            char const *s = argv[0];
            unsigned char c;

            while ((c = *s++)) {
                if (c == '\\' && *s) {
                    switch (c = *s++) {
                        case 'c':
                            return 7;
                        case 'n':
                            c = '\n';
                            break;
                        case 't':
                            c = '\t';
                            break;
                        case 'x': {
                            unsigned char ch = *s;
                            if (!isxdigit(ch)) {
                                goto not_an_escape;
                            }
                            s++;
                            c = hex2dec(ch);
                            ch = *s;
                            if (isxdigit(ch)) {
                                s++;
                                c = c * 16 + hex2dec(ch);
                            }
                        }  break;
                        case '0':
                            c = 0;
                            if (!('0' <= *s && *s <= '7')) {
                                break;
                            }
                            c = *s++;
                            // fallthrough
                        case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                            c -= '0';//1~7の数値
                            if ('0' <= *s && *s <= '7') {
                                c = c * 8 + (*s++ - '0');
                            }
                            if ('0' <= *s && *s <= '7') {
                                c = c * 8 + (*s++ - '0');
                            }
                            break;
                        case '\\':
                            break;
                        not_an_escape:
                        default:
                            putchar('\\');
                            break;
                    }
                }
                putchar(c);
            }
            argc--;
            argv++;
        }
    } else {
        while (argc > 0) {
            fputs(argv[0], stdout);
            argc--;
            argv++;
            if (argc > 0) {
                putchar(' ');
            }
        }

    }
    if (display_return) {
        putchar('\n');
    }
    return EXIT_SUCCESS;
}
