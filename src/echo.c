#include <config.h>  //configureで生成される
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>

#include "system.h"

#define PROGRAM_NAME "my_echo"

#define AUTHORS \
    proper_name("yoshi")

// もし真ならば、デフォルトでバックスラッシュエスケープを解釈します。
//  X/Open Portability Guideという移植性を向上させるためのガイドラインで、デフォルトがfalseなのでXPG仕様に従わずバックスラッシュエスケープはデフォルトで解釈されません
#ifndef DEFAULT_ECHO_TO_XPG
enum {
    DEFAULT_ECHO_TO_XPG = false
};
#endif

void usage(int status) {
    /* STATUS は常に EXIT_SUCCESS でなければなりません（他の多くのユーティリティでは emit_try_help を呼び出すため）。emit_try_help()は失敗した出力のあとにマンページへのリンクを出す */
    //  _()関数は文字列を国際化対応してだす。gettext.h
    assert(status == EXIT_SUCCESS);
    printf(_("\
Usage: %s [SHORT-OPTION]... [STRING]...\n\
  or:  %s LONG-OPTION\n\
"),
           program_name, program_name);
    fputs(_("\
Echo the STRING(s) to standard output.\n\
\n\
  -n             do not output the trailing newline\n\
"),
          stdout);
    // N_はNは翻訳対象文字列のマーク _は文字列を翻訳するマクロ
    fputs(_(DEFAULT_ECHO_TO_XPG
                ? N_("\
  -e             enable interpretation of backslash escapes (default)\n\
  -E             disable interpretation of backslash escapes\n")
                : N_("\
  -e             enable interpretation of backslash escapes\n\
  -E             disable interpretation of backslash escapes (default)\n")),
          // デフォルトは-Eの挙動ですよ。
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    fputs(_("\
\n\
If -e is in effect, the following sequences are recognized:\n\
\n\
"),
          stdout);
    fputs(_("\
  \\\\      backslash\n\
  \\a      alert (BEL)\n\
  \\b      backspace\n\
  \\c      produce no further output\n\
  \\e      escape\n\
  \\f      form feed\n\
  \\n      new line\n\
  \\r      carriage return\n\
  \\t      horizontal tab\n\
  \\v      vertical tab\n\
"),
          stdout);
    fputs(_("\
  \\0NNN   byte with octal value NNN (1 to 3 digits)\n\
  \\xHH    byte with hexadecimal value HH (1 to 2 digits)\n\
"),
          stdout);
    printf(USAGE_BUILTIN_WARNING, PROGRAM_NAME);
    emit_ancillary_info(PROGRAM_NAME);  // 共通のヘルプテキストを出す
    exit(status);
}

// 「引数cを16進数の文字から整数に変換します。」16進数を１０進数に変換
static int
hextobin(unsigned char c) {
    switch (c) {
        default:
            return c - '0';
        case 'a':
        case 'A':
            return 10;
        case 'b':
        case 'B':
            return 11;
        case 'c':
        case 'C':
            return 12;
        case 'd':
        case 'D':
            return 13;
        case 'e':
        case 'E':
            return 14;
        case 'f':
        case 'F':
            return 15;
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

    initialize_main(&argc, &argv);       // VMSというOSのための特別な関数,組み込みのワイルドカード展開を強制
    set_program_name(argv[0]);           // basename argv[0]のような形でメモリに保存する。argv[0] のベースネーム（ファイル名部分）を取得して、program_name という変数に格納します。
    setlocale(LC_ALL, "");               // setlocale(LC_ALL, ""); の呼び出しは、現在のロケールをプログラムが実行される環境のデフォルトのロケールに設定します。LC_ALL は、ロケールカテゴリを指定するための定数であり、すべてのロケールカテゴリに対して設定を行うことを示します。
    bindtextdomain(PACKAGE, LOCALEDIR);  // フリーソフトウェアのgettext.hを使用して、国際化機能のディレクトリを設定します。
    textdomain(PACKAGE);                 // 指定されたテキストドメインを有効にし、プログラム内でのメッセージのローカライズ（翻訳）を可能にします。引数として渡される PACKAGE は、テキストドメインを識別するためのラベルや識別子です。通常、PACKAGE にはソフトウェアパッケージの名前が指定

    atexit(close_stdout);  // プログラムの終了時に自動的に標準出力を閉じる処理を登録するための関数呼び出しです。これにより、プログラムが正常または異常な終了時に標準出力を確実に閉じることができます。

    // 短縮形のオプションを受け入れないために、parse_long_options を使用せずに直接オプションを解析します。echo --help or echo --version が動くようにしている
    if (allow_options && argc == 2) {
        if (STREQ(argv[1], "--help"))
            usage(EXIT_SUCCESS);

        if (STREQ(argv[1], "--version")) {
            version_etc(stdout, PROGRAM_NAME, PACKAGE_NAME, Version, AUTHORS,
                        (char *)NULL);
            return EXIT_SUCCESS;
        }
    }
    // プログラム名を除いたコマンドライン引数に対して処理を行うための準備です。パスパラメタの左シフト的なことをしている
    --argc;
    ++argv;
    // getoptを使わずに直接入力をパースする
    if (allow_options)
        while (argc > 0 && *argv[0] == '-') {
            // オプションの処理ループでは、argv[0] からハイフンを除いた文字列を temp に格納し、その中の文字を順に検査します。
            char const *temp = argv[0] + 1;
            size_t i;

            /* オプションを処理しているように見える場合は、指定された
           オプションが実際に有効かどうかを確認します。有効でない場合は、
           文字列を単にエコーします。 */

            for (i = 0; temp[i]; i++)
                switch (temp[i]) {
                        // -以降の一文字ずつを操作してチェックしている
                        // もし、検査中の文字が 'e'、'E'、'n' のいずれかでない場合、just_echo にジャンプしてオプションではなく単に文字列をエコーする処理に移ります。
                    case 'e':
                    case 'E':
                    case 'n':
                        break;
                    default:
                        goto just_echo;
                }
            // もし、検査中の文字が存在せず、つまりオプションがない場合、just_echo にジャンプしてオプション処理をスキップし、単に文字列をエコーする処理に移ります。
            if (i == 0)
                goto just_echo;

            /* TEMP 内のすべてのオプションが ECHO の有効なオプションです。
           それらを処理します。 */
            //  tempには-以降の文字列が入っている
            while (*temp)
                switch (*temp++) {
                    case 'e':
                        // バックスラッシュによるエスケープを解釈する
                        do_v9 = true;
                        break;

                    case 'E':
                        // バックスラッシュによるエスケープを解釈しない
                        do_v9 = false;
                        break;

                    case 'n':
                        // 最後に改行を出力しない
                        display_return = false;
                        break;
                }
            // オプションの処理が終了したら、argc をデクリメントし、argv をインクリメントして、次の引数に進みます。
            argc--;
            argv++;
        }

just_echo:

    if (do_v9 || posixly_correct) {
        // V9の振る舞いや POSIXLY_CORRECT モードが有効な場合は、エスケープシーケンスを処理して特殊文字に変換しながら出力します
        while (argc > 0) {
            char const *s = argv[0];
            unsigned char c;

            while ((c = *s++))
            // 文字列argv[0]の各文字を処理
            {
                if (c == '\\' && *s)
                // 文字がバックスラッシュであり、かつ次の文字が存在する場合にエスケープシーケンスとして扱う（\を表すためには'\\'としなければならない）
                {
                    // エスケープシーケンスの種類に応じて、対応する特殊文字や制御文字に変換されます
                    switch (c = *s++) {
                        case 'a':
                            c = '\a';
                            break;
                        case 'b':
                            c = '\b';
                            break;
                        case 'c':
                            return EXIT_SUCCESS;
                        case 'e':
                            c = '\x1B';
                            break;
                        case 'f':
                            c = '\f';
                            break;
                        case 'n':
                            c = '\n';
                            break;
                        case 'r':
                            c = '\r';
                            break;
                        case 't':
                            c = '\t';
                            break;
                        case 'v':
                            c = '\v';
                            break;
                        case 'x': {
                            // 16進数エスケープシーケンス。次の2つの文字を16進数として解釈し、対応する文字に変換します。
                            unsigned char ch = *s;
                            if (!isxdigit(ch))
                                // 文字列sの最初の文字が16進数の文字でなければ
                                goto not_an_escape;  // ex echo -e '\xp'これだと単なる\xpとする
                            s++;
                            // ヘルパー関数としては、hextobin()があり、これは名前に反して十六進数を十進数に変換する機能。バックスラッシュエスケープ文字の解釈に使う
                            c = hextobin(ch);
                            ch = *s;
                            if (isxdigit(ch)) {
                                s++;
                                c = c * 16 + hextobin(ch);
                            }
                        } break;
                            // 8進数エスケープシーケンス。最大3桁の8進数を解釈し、対応する文字に変換します。
                        case '0':
                            c = 0;
                            if (!('0' <= *s && *s <= '7'))
                                break;  // 次の文字が8進数として適正でなければ、breakする
                            c = *s++;   // そうでなければ、sを一文字勧めつつ、今刺している文字をcにいれる。ここではbreakせずにフォールスルーする
                            FALLTHROUGH;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                        case '7':
                            c -= '0';                    // この計算をすると8進数の値を取得できるex'1'-'0': 49-48
                            if ('0' <= *s && *s <= '7')  // 8進数は3桁なので、3文字進める
                                c = c * 8 + (*s++ - '0');
                            if ('0' <= *s && *s <= '7')
                                c = c * 8 + (*s++ - '0');
                            break;
                            // バックスラッシュ文字自体を出力
                        case '\\':
                            break;

                        not_an_escape:
                        default:
                            putchar('\\');
                            break;
                    }
                }
                // エスケープ処理が終了すると、変換された文字を putchar() を使って出力します。
                putchar(c);
            }
            // 次のコマンドライン引数へと処理をすすめる
            argc--;
            argv++;
            if (argc > 0)
                // まだ次に引数があるならスペースを出力しておく
                putchar(' ');
        }
    } else {
        // 単に引数をそのまま出力。
        while (argc > 0) {
            fputs(argv[0], stdout);
            argc--;
            argv++;
            if (argc > 0)
                putchar(' ');
        }
    }

    // 改行文字を出力してプログラムを正常に終了する
    if (display_return)
        putchar('\n');
    return EXIT_SUCCESS;
}
