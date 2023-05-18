/* Unix の cat との相違点：
   * 常にバッファなし、-uは無視されます。
   * 通常、他のバージョンのcatよりはるかに高速で、その差は-vオプションを使用した場合に特に顕著です。
   by tege@sics.se, Torbjorn Granlund, advised by rms, Richard Stallman.  */
#include <config.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/types.h>

#if HAVE_STROPTS_H
#include <stropts.h>
#endif
#include <sys/ioctl.h>

#include "die.h"
#include "error.h"
#include "fadvise.h"
#include "full-write.h"
#include "ioblksize.h"
#include "safe-read.h"
#include "system.h"
#include "xbinary-io.h"

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "my_cat"

#define AUTHORS \
    proper_name("yoshi")

/* Name of input file.  May be "-".  */
static char const *infile;//入力ファイル名を指定するために定義

/* Descriptor on which input file is open.  */
static int input_desc;//ファイルディスクリプタを保持するために定義

/* Buffer for line numbers.
   An 11 digit counter may overflow within an hour on a P2/466,
   an 18 digit counter needs about 1000y */
#define LINE_COUNTER_BUF_LEN 20
static char line_buf[LINE_COUNTER_BUF_LEN] =
    {
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '0',
        '\t', '\0'};//行数を管理するため

/* line_bufの中で印刷を開始する位置を指定します。これは、行数が999999より大きくなければ変化しない。 */
// この`line_buf`は、行番号を表示するためのバッファです。初期状態では、バッファは空白文字（' '）で埋められ、最後にタブ文字（'\t'）とヌル文字（'\0'）が追加されています。これにより、行番号を表示する際には、前方が空白で、後方がタブで区切られたフォーマットになります。バッファのサイズ（`LINE_COUNTER_BUF_LEN`）は、必要な行番号の最大桁数（おそらく最大で7桁）に余裕を持たせて設定されていると思われます。

// `line_num_print`は`line_buf`内で印刷開始位置を指示するポインタで、初期値は`line_buf + LINE_COUNTER_BUF_LEN - 8`となっています。これはバッファの末尾から8文字前を指しています。このポインタ位置から行番号が表示され、行番号が1000000（7桁）以上になると、印刷開始位置は左に移動して行きます。

// したがって、このバッファとポインタは、行番号を整形して表示するためのもので、行番号の前に適切な数の空白を置くことで、行番号の表示を右揃えに保つために使われています。
static char *line_num_print = line_buf + LINE_COUNTER_BUF_LEN - 8;//12

/* 'line_buf'の1桁目の位置。 */
static char *line_num_start = line_buf + LINE_COUNTER_BUF_LEN - 3;//17

/* 'line_buf'の最後の桁の位置。 */
static char *line_num_end = line_buf + LINE_COUNTER_BUF_LEN - 3;//17

static int newlines2 = 0;/* 'cat'関数のローカルな'改行'を呼び出しの間に保持する。 */

void usage(int status) {
    if (status != EXIT_SUCCESS)
        emit_try_help();
    else {
        printf(_("\
Usage: %s [OPTION]... [FILE]...\n"),
               program_name);
        fputs(_("Concatenate FILE(s) to standard output.\n"), stdout);

        emit_stdin_note();

        fputs(_("\n\
  -A, --show-all           equivalent to -vET\n\
  -b, --number-nonblank    number nonempty output lines, overrides -n\n\
  -e                       equivalent to -vE\n\
  -E, --show-ends          display $ at end of each line\n\
  -n, --number             number all output lines\n\
  -s, --squeeze-blank      suppress repeated empty output lines\n\
"),
              stdout);
        fputs(_("\
  -t                       equivalent to -vT\n\
  -T, --show-tabs          display TAB characters as ^I\n\
  -u                       (ignored)\n\
  -v, --show-nonprinting   use ^ and M- notation, except for LFD and TAB\n\
"),
              stdout);
        fputs(HELP_OPTION_DESCRIPTION, stdout);
        fputs(VERSION_OPTION_DESCRIPTION, stdout);
        printf(_("\n\
Examples:\n\
  %s f - g  Output f's contents, then standard input, then g's contents.\n\
  %s        Copy standard input to standard output.\n\
"),
               program_name, program_name);
        emit_ancillary_info(PROGRAM_NAME);
    }
    exit(status);
}

/* 行番号を文字列で作り上げる  */
// もちろんです、それぞれの行とそのポインタ操作について説明します。

// 1. `char *endp = line_num_end;`
//    ここでは、`endp`という新しいポインタを作成し、それを`line_num_end`が指す場所に初期化しています。`line_num_end`は行番号の最後の桁を指すので、ここでは行番号の最後の桁について操作を行います。

// 2. `do {`
//    ここでは`do-while`ループを開始しています。このループは、各桁についてチェックし、必要に応じて桁上げを行います。

// 3. `if ((*endp)++ < '9')`
//    ここでは`endp`が指す文字をインクリメントしています。そして、そのインクリメントした値が`'9'`より小さいかどうかをチェックしています。もし`'9'`より小さい場合、その桁でのキャリーオーバーは不要であるため、ループから抜け出します。

// 4. `*endp-- = '0';`
//    もし`endp`が指す文字が`'9'`だった場合（つまり、その桁でキャリーオーバーが必要な場合）、その桁を`'0'`にリセットし、左の桁（`endp`の1つ前）に移動します。

// 5. `} while (endp >= line_num_start);`
//    ここで`do-while`ループが終了します。`endp`が`line_num_start`以上である限り、ループは続きます。これは、行番号の左端に達するまでキャリーオーバーを続けるためです。

// 6. `if (line_num_start > line_buf)`
//    ここでは、行番号の左端`line_num_start`が全体のバッファ`line_buf`を越えていないかを確認しています。越えていない場合、次の行で行番号の左端を1つ左に移動します。

// 7. `*--line_num_start = '1';`
//    ここでは、行番号の左端`line_num_start`を1つ左に移動し、その新しい位置に`'1'`を設定します。これは、すべての桁がキャリーオーバーした場合（例えば`'999'`から`'1000'`になる場合）に行われます。

// 8. `else *line_buf = '>';`
//    もし`line_num_start`が`line_buf`を越えてしまった場合（つまり、行番号がバッファの大きさを超えてしまった場合）、バッファの最初の位置に`'>'`を設定します。これは、行番号がバッ
static void
next_line_num(void) {
    char *endp = line_num_end;
    do {
        if ((*endp)++ < '9')
            return;
        *endp-- = '0';
    } while (endp >= line_num_start);
    if (line_num_start > line_buf)
        *--line_num_start = '1';
    else
        *line_buf = '>';
    if (line_num_start < line_num_print)
        line_num_print--;
}

/* プレーンなcat。 input_desc' の後ろにあるファイルを
STDOUT_FILENO にコピーする。
成功すればtrueを返す。 */
// 入力から出力への基本的なコピー
static bool simple_cat(
    /* Pointer to the buffer, used by reads and writes.  */
    // 読み書きするバッファーへのポインタを第一引数に受け取る
    char *buf,
    /* Number of characters preferably read or written by each read and write
       call.  */
    //  読み込むサイズを指定
    size_t bufsize) {
    /* Actual number of characters read, and therefore written.  */
    // 実際の読み込むバイトサイズ、したがって、書き込むバイトサイズ
    size_t n_read;
    // EOFまでループする
    while (true) {
        /* Read a block of input.  */
        // bufsizeだけbufに読み込む、input_descから（インプットディスクリプター）
        // safe_read()割り込みで再試行する読み込み
        n_read = safe_read(input_desc, buf, bufsize);
        if (n_read == SAFE_READ_ERROR) {
          // 読み込みにエラーがあった
            error(0, errno, "%s", quotef(infile));
            // エラーメッセージを標準エラーに出力し、プロセスを終了させる
            return false;
        }
        if (n_read == 0) {
          // EOFだった
          return true;
        }

        /* Write this block out.  */
        // ブロックを書き出す
        {
          // n_readは読み込めたバイト数
            /* The following is ok, since we know that 0 < n_read.  */
            size_t n = n_read;
            if (full_write(STDOUT_FILENO, buf, n) != n) {
            // STDOUT_FILENOが参照するファイルにbufからnバイト書き込む
            // 失敗したらdieする
                die(EXIT_FAILURE, errno, _("write error"));
            }
        }
    }
}

/* Write any pending output to STDOUT_FILENO.
   Pending is defined to be the *BPOUT - OUTBUF bytes starting at OUTBUF.
   Then set *BPOUT to OUTPUT if it's not already that value.  */

static inline void //保留中のデータをすべて書き込む
//外部の非標準ヘルパー
write_pending(char *outbuf, char **bpout) {
    size_t n_write = *bpout - outbuf;
    if (0 < n_write) {
        if (full_write(STDOUT_FILENO, outbuf, n_write) != n_write)
            die(EXIT_FAILURE, errno, _("write error"));
        *bpout = outbuf;
    }
}

/* Cat the file behind INPUT_DESC to the file behind OUTPUT_DESC.
   Return true if successful.
   Called if any option more than -u was specified.

   A newline character is always put at the end of the buffer, to make
   an explicit test for buffer end unnecessary.  */
/* INPUT_DESC の後ろにあるファイルを OUTPUT_DESC の後ろにあるファイルに取り込む。
   成功すればtrueを返す。
   u以上のオプションが指定された場合に呼び出される。

   バッファの終わりを明示的に調べる必要がないように、バッファの終わりには常に改行文字が置かれる。
   バッファの終了を明示的にテストする必要がないためです。 */
static bool //I/Oコピーのためのすべての機能を実装している
cat(
// inbuf (char *): これは入力バッファの開始位置を示すポインタです。このバッファには関数が読み取るべきデータが格納されます。
    char *inbuf,

    // insize (size_t): これは各読み取り呼び出しで読み取られる文字の数を示します。つまり、関数が一度に読み込むデータのサイズを定義します。
    size_t insize,

    // outbuf (char *): これは出力バッファの開始位置を示すポインタです。関数がデータを処理した後、その結果はこのバッファに書き込まれます。
    char *outbuf,

    // outsize (size_t): これは各書き込み呼び出しで書き込まれる文字の数を示します。つまり、関数が一度に書き込むデータのサイズを定義します。
    size_t outsize,

    /* Variables that have values according to the specified options.  */
    bool show_nonprinting,//-v
    bool show_tabs,//-T
    bool number,//-n
    bool number_nonblank,//-b
    bool show_ends,//-E
    bool squeeze_blank /* -s */) {
      // cat()とsimple_cat()の比較
// 入力がそのまま出力にコピーされるのであれば、simple_cat()が適しています。
// しかし、追加のフォーマットが要求される場合
// （行番号や非印刷物など）、完全なcat()関数が呼び出されます。
// 後者は、40行に対して300行と、より複雑です。

// １　初期化
// ch (unsigned char): これは最後に入力バッファから読み取った文字を保持します。文字は1バイト（unsigned char）で表現され、この変数はその文字を保存しています
    unsigned char ch;

    // bpin (char *): これは「Buffer Pointer IN」の略で、入力バッファで次に読むべき文字の場所を指すポインタです。つまり、bpinが指す場所にはまだ読んでいないデータが存在します。
    char *bpin;

    // eob (char *): これは「End Of Buffer」の略で、入力バッファの終わり、つまり有効なデータが存在しないバッファの最初の位置を指すポインタです。bpinがeobと等しくなった時、バッファが空であり、新たなデータを読み込む必要があることを示します。
    char *eob;
// bpout (char *): これは「Buffer Pointer OUT」の略で、出力バッファのどこに次の文字を書き込むべきかを示すポインタです。つまり、bpoutが指す場所にはまだ書き込まれていないスペースがあります。
    char *bpout;

    // これは最後の読み込み呼び出しによって読み取られた文字数を保持します。size_tは非負整数の型で、ここでは読み込んだ文字数を表すのに適しています。
    size_t n_read;
// この変数 `newlines` は、入力中に連続していくつの改行文字（'\n'）が現れたかを追跡するためのものです。その値によって、後続の処理の"状態"が決まります。ここでの "状態"とは、一連のテキストが新しい行の開始なのか、それとも連続する改行による空行なのか、等を示します。

// 具体的には：

// - `newlines` が -1 のとき：これは直前の文字が改行でなかったことを示し、新しい行の開始を意味します。
// - `newlines` が 0 のとき：これは直前の文字が改行であり、まだ新しい行が始まっていないことを示します。
// - `newlines` が 1 のとき：これは直前に2つの連続する改行があったことを示し、空行があったことを示します。
// - `newlines` が 2 以上のとき：これは直前に3つ以上の連続する改行があったことを示し、複数の空行があったことを示します。

// この変数は、特に `-n`, `-b`, `-s` オプションが有効なときに重要となります。これらのオプションはそれぞれ行番号の表示、非空白行に対する行番号の表示、連続する空行の圧縮を制御するためのものです。これらのオプションが有効なとき、`newlines` の値に基づいてどのような処理を行うかが決まります。
    int newlines = newlines2;
// これは、プログラムが FIONREAD ioctl を使用して最適化を行うべきかどうかを示すフラグです。
#ifdef FIONREAD
    /* 非ゼロの場合(true)、最適化としてFIONREAD ioctlを使用します。
       (Ultrixでは、NFSファイルシステムでサポートされていません。) */
    bool use_fionread = true;
#endif

    /* BPIN＞EOBとなるようにinbufポインタを初期化し，入力を即座に読み込む。が即座に読み込まれます。 */

    eob = inbuf;//eobは入力バッファの先頭にセットされる
    bpin = eob + 1;//入力バッファが現時点では空を示す。bpin > eobとなる。こうなることで、最初のループの評価時にバッファが空であると判断させることができる。これにより、すぐに新たな入力の読み込みが行われる

    bpout = outbuf;//出力バッファの現在の書き込み位置が出力バッファの先頭にセットされる。これにより、最初の書き込みが出力バッファの先頭から始まるようになる。書き込み操作がバッファの先頭から開始されるようにする

    while (true) {
        // 無限ループ
        do {
            /* OUTBUFにOUTSIZE以上のバイトがあれば書き込む。 */
        // ２a出力バッファが満タンになったときにその内容を出力する処理
//         このコードブロックは、出力バッファが一杯になった場合（`outbuf + outsize <= bpout`）に実行されます。出力バッファが一杯になったというのは、つまり、出力バッファに書き込まれたデータのサイズが、バッファのサイズ（`outsize`）を超えた場合を指します。

// この場合、次の操作が実行されます：

// 1. バッファ内のデータを出力するために、`full_write()`関数が呼び出されます。この関数は、出力バッファのデータを標準出力（`STDOUT_FILENO`）に書き込みます。書き込むデータのサイズは`outsize`です。

// 2. 書き込み操作がエラーを返した場合（書き込みサイズが`outsize`と等しくない場合）、エラーメッセージが表示され、プログラムは終了します。

// 3. 成功した書き込み操作の後、書き込みポインタ（`wp`）は`outsize`だけ進みます。

// 4. バッファにまだ書き込まれていないデータ（`remaining_bytes`）がある場合、このループは続行します。つまり、全てのデータが書き込まれるまで、`full_write()`呼び出しとポインタの進行が繰り返されます。

// 5. 全てのデータが書き込まれた後、バッファ内にまだ書き込まれていない残りのデータ（`remaining_bytes`）がバッファの先頭に移動されます。これは`memmove()`関数によって行われます。この操作は、次のデータの読み取りと書き込みのためにバッファをクリアする役割を果たします。

// 6. 最後に、出力ポインタ（`bpout`）は、新しく書き込むべき位置、つまり移動されたデータの末尾に設定されます。

// このコードブロックの主な目的は、バッファが一杯になったときにデータを書き込み、バッファをクリアすることで、次のデータの書き込みを可能にすることです。
            if (outbuf + outsize <= bpout) {
                // この判定はポインタとバッファサイズの操作に基づいています。

// `outbuf`は出力バッファの先頭を指すポインタで、`outsize`はバッファの大きさ（容量）を表す値です。`bpout`は出力バッファ内の「次に書き込むべき位置」を指すポインタです。したがって、`bpout`が`outbuf + outsize`（バッファの先頭 + バッファのサイズ = バッファの末尾）に達するということは、出力バッファが一杯になったということを意味します。

// 具体的には、`bpout`が`outbuf`から`outsize`バイト以上先に進んだ場合、つまり`outbuf + outsize <= bpout`となった場合、バッファは一杯で、新たなデータの書き込みがバッファのサイズを超えると判断されます。これは、`bpout`が「次に書き込むべき位置」を指しているため、`bpout`がバッファの末尾を超えるということは、すでにバッファが一杯であるということを意味します。
                char *wp = outbuf;//wp書き込みポインタ
                size_t remaining_bytes;
                do {
                    if (full_write(STDOUT_FILENO, wp, outsize) != outsize)
                        die(EXIT_FAILURE, errno, _("write error"));
                    wp += outsize;

//                     `remaining_bytes = bpout - wp;` この式はポインタの差分を取る操作です。`bpout`は出力バッファ内の「次に書き込むべき位置」を指すポインタで、`wp`は現在書き込んでいる位置を指すポインタです。

// したがって、`remaining_bytes = bpout - wp;`は、出力バッファ内でまだ書き込まれていないバイト数（残りのバイト数）を計算します。

// ポインタ同士の減算操作は、そのポインタが指すデータ型の単位で差分を計算します。この場合、`char`型ポインタなので、`bpout - wp`は「`bpout`が指す場所から`wp`が指す場所までの`char`型データの数」を表します。これはバイト単位での差分と等しくなります。

// 具体的には、`wp`がバッファの先頭を指し、`bpout`がその先の何かの位置を指している場合、`remaining_bytes = bpout - wp;`は`bpout`と`wp`の間にあるバイト数を計算します。
                } while (outsize <= remaining_bytes);

                /* 残りのバイトをバッファの先頭に移動させる。
バッファの先頭に移動させます。 */

                memmove(outbuf, wp, remaining_bytes);
                bpout = outbuf + remaining_bytes;
            }

            /* Is INBUF empty?  */
            // 2b　入力バッファが空になったときに新たな内容を読み込む処理
            if (bpin > eob) {
                // このコードでは、`bpin`が指す場所が`eob`（End of Buffer）を超えているかどうかをチェックしています。具体的には、入力バッファから読み取るべき新たなデータがない（すべて読み取り終わった）ことを示しています。

// `bpin`は"Buffer Pointer for INput"の略で、入力バッファの現在の読み取り位置を指しています。一方、`eob`は"End Of Buffer"の略で、入力バッファの終端を指しています。したがって、`bpin > eob`という条件は「現在の読み取り位置がバッファの終端を超えているか？」ということを確認しています。

// もし`bpin > eob`が真であれば、それは入力バッファ内の現在のデータをすべて読み終わった（あるいはまだ何も読んでいない）、つまり新たなデータを読み込む必要があるという状態を意味します。これにより、次のデータ読み取りを準備するための`input_pending`フラグが`false`に設定されます。
                bool input_pending = false;
#ifdef FIONREAD
// FIONREADが利用可能な環境では、これにより未読データが存在する場合のみ読み取り操作を行い、それ以外の場合には不要な読み取り操作を避けることが可能になり、プログラムのパフォーマンスを向上させることができます。ただし、FIONREADがサポートされていない環境や、特定のエラーが発生した場合には、この方法を使用しないようにコードが設計されています。
                int n_to_read = 0;

            /* すぐに読むべき入力があるか？
ない場合は、これから待つことになります、 ので、待つ前にバッファリングされた出力をすべて書き込んでください。 */
// use_fionread フラグが真である場合、FIONREAD ioctl を使って未読データのバイト数を n_to_read に取得しようとしています
                if (use_fionread && ioctl(input_desc, FIONREAD, &n_to_read) < 0) {
                    /* Ultrix は NFS で EOPNOTSUPP を返します；
                       HP-UXはパイプでENOTTYを返します。
                       SunOSはEINVALを返し
                       More/BSDは/dev/nullのような特殊なファイルに対してENODEVを返します。
                       のような特殊なファイルではENODEVを返す。
                       Irix-5 はパイプで ENOSYS を返します。 */
                    if (errno == EOPNOTSUPP || errno == ENOTTY || errno == EINVAL || errno == ENODEV || errno == ENOSYS)
                        use_fionread = false;
                    else {
                        error(0, errno, _("cannot do ioctl on %s"),
                              quoteaf(infile));
                        newlines2 = newlines;
                        return false;
                    }
                }
                if (n_to_read != 0) {
// 未読データがあれば、後続の処理で未読データがあることをわかるようにするためにフラグをたてる
                    input_pending = true;
                }
#endif

                if (!input_pending)
                //保留中のデータをすべて書き込む外部の非標準ヘルパー
                    write_pending(outbuf, &bpout);

                /* INBUFにさらに入力を読み込む。 */
                // 末尾処理: 最後に、ファイルの終端に達したときやエラーが発生したときの処理を行っています。具体的には、バッファに残ったデータの出力とエラーメッセージの表示が行われます。
                n_read = safe_read(input_desc, inbuf, insize);
                if (n_read == SAFE_READ_ERROR) {
                    // エラー発生
                    error(0, errno, "%s", quotef(infile));
                    write_pending(outbuf, &bpout);
                    newlines2 = newlines;
                    return false;
                }
                if (n_read == 0) {
                    // EOFに達した
                    write_pending(outbuf, &bpout);
                    newlines2 = newlines;
                    return true;
                }

/* ポインターを更新し、バッファエンドにセンチネルを挿入します。 */
// bpin = inbuf; eob = bpin + n_read; *eob = '\n'; これらの行では、バッファ内の新たなデータの位置を設定し、バッファの終端にセンチネル（終端を表す特殊な値）として改行文字を挿入しています。これにより、バッファの終端を明示的に示すことで、バッファオーバーフロー（バッファの範囲を超えたアクセス）を防ぐことができます。

                bpin = inbuf;
                eob = bpin + n_read;
                *eob = '\n';
            } else {
                /* 本物の（センチネルではない）改行でした。 */
                /* 最後の行は空でしたか？
                   (つまり、2つ以上の連続した改行が読み込まれたか) */
//todo ３．改行文字の処理　行番号の出力や連続する空行の圧縮などが行われます
                if (++newlines > 0) {
                    if (newlines >= 2) {
                        /* ここでは2個までとする。 そうでないと、連続した改行が多い場合 連続した改行があると、カウンターはINT_MAXで折り返すことができます。 */
                        /* 複数の隣接する空行を同上(-s)で置換する場合、この空行は2行目だったのでしょうか？ */
                        if (squeeze_blank) {
                            ch = *bpin++;
                            continue;
                        }
                    }

                    /* 空行(-n)に行番号を書きますか？ */

                    if (number && !number_nonblank) {
                        next_line_num();//行番号バッファを更新する。
                        bpout = stpcpy(bpout, line_num_print);
                    }
                }

                /* (-e).$を出力  */

                if (show_ends)
                    *bpout++ = '$';

                /* 改行を出力.  */

                *bpout++ = '\n';
            }
            ch = *bpin++;
        } while (ch == '\n');//chは最後に入力バッファから読み取った１文字。したがって、読み取ったものが改行文字の間繰り返す

        /* 行頭であり、行番号が要求されているか？ */
        // newlinesは最後に読み込んだ文字が改行であるかどうかを示す変数。改行が連続で出現した場合、この値はそれらの連続する改行の数になります。最後に読み込んだ文字が改行でなければ、この値は-1になります。したがって、newlines >= 0は新しい行が始まったという状態を示す
        if (newlines >= 0 && number) {
            next_line_num();
            // line_num_printは出力するべき行番号を格納した文字列の開始位置を指すポインタ.next_line_num()によって更新され、最新の行番号を常に保持
            bpout = stpcpy(bpout, line_num_print);
            // bpoutは出力バッファの現在のいちを指すポインタ。line_num_printが指す文字列（行番号）をbpoutが指す位置にコピーし、その後、コピーした文字列の末尾のいちを返します。その結果bpoutは更新され、次の出力は行番号の直後から開始される
        }

        /* ここでCHは改行文字を含むことができません。 */

        /* バッファが空になったか、適切な改行が見つかったことを意味する改行文字が見つかるまでは、以下のループが続きます。 */
        /* quoting、すなわち-v、-e、-tのうち少なくとも1つが指定されている場合、 変換が必要な文字列をスキャンします。 */
        //    4非印刷文字の処理: この部分では、読み込んだ文字が非印刷文字（制御文字やASCII範囲外の文字）だった場合の処理を行っています。具体的には、これらの文字を可視化するための変換が行われます。
        if (show_nonprinting) {
            while (true) {
                if (ch >= 32) {
                    // 特殊文字ではなくて
                    if (ch < 127)
                    // asciiにある文字なら
                        *bpout++ = ch;
                    else if (ch == 127) {
                        // DELなら
                        *bpout++ = '^';
                        *bpout++ = '?';
                    } else {
                        // asciiの表にないなら
                        *bpout++ = 'M';
                        *bpout++ = '-';
                        if (ch >= 128 + 32) {
                            if (ch < 128 + 127)
                                *bpout++ = ch - 128;
                            else {
                                *bpout++ = '^';
                                *bpout++ = '?';
                            }
                        } else {
                            *bpout++ = '^';
                            *bpout++ = ch - 128 + 64;
                        }
                    }
                } else if (ch == '\t' && !show_tabs)
                    *bpout++ = '\t';
                else if (ch == '\n') {
                    newlines = -1;
                    break;
                } else {
                    *bpout++ = '^';
                    *bpout++ = ch + 64;//@:64,A:65~なので、0-31の範囲の制御文字を対応する64-95の範囲の大文字アルファベットといくつかの記号にマッピングするためのテクニック
                }

                ch = *bpin++;
            }
        } else {
            /* -v, -e, -tのいずれも指定されておらず、引用されていない。 */
            while (true) {
                if (ch == '\t' && show_tabs) {
                    *bpout++ = '^';
                    *bpout++ = ch + 64;//&\t':9 I:73
                } else if (ch != '\n')
                    *bpout++ = ch;
                else {
                    newlines = -1;
                    break;
                }

                ch = *bpin++;
            }
        }
    }
}

int main(int argc, char **argv) {
    /* 出力のi/o操作の最適なサイズ。 */
    size_t outsize;

    /* 入力のi/o操作の最適なサイズ。 */
    size_t insize;

    size_t page_size = getpagesize();//システムのメモリページのサイズを格納する
    // 4kが一般的

    // 入力バッファを指すポインタ
    char *inbuf;

    // 出力バッファを指すポインタ
    char *outbuf;

    bool ok = true;//実行が成功したことを示すフラグ
    int c;//解析のための次のオプション文字を保持する．

    /* argvのインデックスで引数を処理する。 */
    int argind;

    dev_t out_dev;//出力デバイス番号

    ino_t out_ino;//出力のinode番号

    bool out_isreg;//出力がプレーンファイルであるかどうかのフラグ

    /* 標準入力を読んだことがある場合は、非ゼロとする。 */
    bool have_read_stdin = false;//標準入力から読むかどうかのフラグ

    struct stat stat_buf;

    /* 指定されたオプションに従って設定される変数です。 */
    bool number = false;//-n
    bool number_nonblank = false;//-b
    bool squeeze_blank = false;//-s
    bool show_ends = false;//-E
    bool show_nonprinting = false;//-v
    bool show_tabs = false;//-T
    int file_open_mode = O_RDONLY;//ファイルモードを保持するビットマップ．

    static struct option const long_options[] =
        {
            //空行以外に行番号をつける
            {"number-nonblank", no_argument, NULL, 'b'},
            // 全ての行に行番号を付ける
            {"number", no_argument, NULL, 'n'},//
            //連続した空行の出力を行わない
            {"squeeze-blank", no_argument, NULL, 's'},
            //^ や M- 表記を使用する (LFD と TAB は除く)
            {"show-nonprinting", no_argument, NULL, 'v'},
            //行の最後に $ を付ける
            {"show-ends", no_argument, NULL, 'E'},
            //TAB 文字を ^I で表示
            {"show-tabs", no_argument, NULL, 'T'},
            //-vETと同じ
            {"show-all", no_argument, NULL, 'A'},
            {GETOPT_HELP_OPTION_DECL},
            {GETOPT_VERSION_OPTION_DECL},
            {NULL, 0, NULL, 0}};

    initialize_main(&argc, &argv);//ワイルドカード展開に必要
    set_program_name(argv[0]);//program_nameにargv[0]を格納する
    setlocale(LC_ALL, "");//ロケール設定
    bindtextdomain(PACKAGE, LOCALEDIR);//翻訳対応
    textdomain(PACKAGE);//翻訳対応

    // case_GETOPT_HELP_CHAR or case_GETOPT_VERSION_CHAR code.を経由して標準出力を閉じるように手配して。
    atexit(close_stdout);//きちんと標準出力が閉じられるようにする

    while ((c = getopt_long(argc, argv, "benstuvAET", long_options, NULL)) != -1) {
        switch (c) {
            case 'b'://空行以外に行番号を付ける。-n より優先される
                number = true;
                number_nonblank = true;
                break;

            case 'e'://-vE と同じ
                show_ends = true;//-E
                show_nonprinting = true;//-v
                break;

            case 'n'://全ての行に行番号を付ける
                number = true;
                break;

            case 's'://連続した空行の出力を行わない
                squeeze_blank = true;
                break;

            case 't'://-vT と同じ
                show_tabs = true;//-T
                show_nonprinting = true;//-v
                break;

            case 'u'://無視されるオプション
                /* -u 機能を無条件で提供します。 */
                break;

            case 'v'://^ や M- 表記を使用する (LFD と TAB は除く)
                show_nonprinting = true;
                break;

            case 'A'://-vET と同じ
                show_nonprinting = true;//-v
                show_ends = true;//-E
                show_tabs = true;//-T
                break;

            case 'E'://行の最後に $ を付ける
                show_ends = true;//-E
                break;

            case 'T'://TAB 文字を ^I で表示
                show_tabs = true;//-T
                break;

                case_GETOPT_HELP_CHAR;// --helpを処理

                case_GETOPT_VERSION_CHAR(PROGRAM_NAME, AUTHORS);
                // --versionを処理

            default:
            //それいがいの文字ならエラーメッセージ
                usage(EXIT_FAILURE);
        }
    }

    // 標準出力に関する情報を取得
    if (fstat(STDOUT_FILENO, &stat_buf) < 0)
    // 失敗したら
        die(EXIT_FAILURE, errno, _("standard output"));

    outsize = io_blksize(stat_buf);//最適なブロックサイズを取得する
    out_dev = stat_buf.st_dev;
    out_ino = stat_buf.st_ino;
    out_isreg = S_ISREG(stat_buf.st_mode) != 0;

    if (!(number || show_ends || squeeze_blank)) {
      // 行番号出力、行の最後に$、連続した空行の出力を行わない。
      // これらすべてがfalseだと、file_open_modeを...にする
        file_open_mode |= O_BINARY;
        xset_binary_mode(STDOUT_FILENO, O_BINARY);
        // 標準出力をバイナリモードにする
    }

/* 入力ファイルの中に、出力ファイルと同じものがあるかどうかを確認します。 */
    /* Main loop.  */

    infile = "-";
    argind = optind;//catする引数のargvインデックス

    do {
        if (argind < argc)//オプションをすべて解析したあとの、
        // オプションではない引数がargcより小さいというのは、
        // 最後にファイル名が指定されたということ
        // この場合は、入力ファイルをそのファイルにする
            infile = argv[argind];

        if (STREQ(infile, "-")) {
          // 標準入力から読む
            have_read_stdin = true;
            input_desc = STDIN_FILENO;
            if (file_open_mode & O_BINARY)
                xset_binary_mode(STDIN_FILENO, O_BINARY);
        } else {
          // ファイルから読む
            input_desc = open(infile, file_open_mode);
            if (input_desc < 0) {
              // openできなかったときのエラー処理
                error(0, errno, "%s", quotef(infile));
                ok = false;
                // 読めなかったら次の引数へ
                continue;
            }
        }

        if (fstat(input_desc, &stat_buf) < 0) {
          // 入力元のファイル情報を取得する
            error(0, errno, "%s", quotef(infile));
            ok = false;
            goto contin;
        }
        insize = io_blksize(stat_buf);//最適なブロックサイズを取得する

        fdadvise(input_desc, 0, 0, FADVISE_SEQUENTIAL);

        /* 空でない通常ファイルをそれ自体にコピーしてはいけない、それは単に出力デバイスを使い果たすだけだからだ。このエラーは、後で発見するよりも、早めに発見する方がよいでしょう。 */

        if (out_isreg && stat_buf.st_dev == out_dev && stat_buf.st_ino == out_ino && lseek(input_desc, 0, SEEK_CUR) < stat_buf.st_size) {
            error(0, 0, _("%s: input file is output file"), quotef(infile));
            ok = false;
            goto contin;
        }

        /* フォーマット指向のオプションが与えられている場合は 'cat' を、そうでない場合は 'simple_cat' を使用します。 */
        if (!(number || show_ends || show_nonprinting || show_tabs || squeeze_blank)) {
            insize = MAX(insize, outsize);
            inbuf = xmalloc(insize + page_size - 1);
// ptr_align() 返されたポインタがメモリアラインされていることを確認する
            ok &= simple_cat(ptr_align(inbuf, page_size), insize);
        } else {
            inbuf = xmalloc(insize + 1 + page_size - 1);

            /* Why are
               (OUTSIZE - 1 + INSIZE * 4 + LINE_COUNTER_BUF_LEN + PAGE_SIZE - 1)
               bytes allocated for the output buffer?

               A test whether output needs to be written is done when the input
               buffer empties or when a newline appears in the input.  After
               output is written, at most (OUTSIZE - 1) bytes will remain in the
               buffer.  Now INSIZE bytes of input is read.  Each input character
               may grow by a factor of 4 (by the prepending of M-^).  If all
               characters do, and no newlines appear in this block of input, we
               will have at most (OUTSIZE - 1 + INSIZE * 4) bytes in the buffer.
               If the last character in the preceding block of input was a
               newline, a line number may be written (according to the given
               options) as the first thing in the output buffer. (Done after the
               new input is read, but before processing of the input begins.)
               A line number requires seldom more than LINE_COUNTER_BUF_LEN
               positions.

               Align the output buffer to a page size boundary, for efficiency
               on some paging implementations, so add PAGE_SIZE - 1 bytes to the
               request to make room for the alignment.  */

            outbuf = xmalloc(outsize - 1 + insize * 4 + LINE_COUNTER_BUF_LEN + page_size - 1);

            ok &= cat(ptr_align(inbuf, page_size), insize,
                      ptr_align(outbuf, page_size), outsize, show_nonprinting,
                      show_tabs, number, number_nonblank, show_ends,
                      squeeze_blank);

            free(outbuf);
        }

        free(inbuf);

    contin:
        if (!STREQ(infile, "-") && close(input_desc) < 0) {
            error(0, errno, "%s", quotef(infile));
            ok = false;
        }
    } while (++argind < argc);//catする引数のインデックスを次にすすめて、
    // それがargcよりも小さい間繰り返す。すなわち、すべてのファイルを処理する

    if (have_read_stdin && close(STDIN_FILENO) < 0)
    // 標準入力から読んでいて、それが正常に閉じれなかった場合
        die(EXIT_FAILURE, errno, _("closing standard input"));

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
