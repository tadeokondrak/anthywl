#include "buffer.h"

#include <stdlib.h>
#include <string.h>

static char const *const ones[128] = {
    ['-'] = "ー",
    ['a'] = "あ",
    ['e'] = "え",
    ['i'] = "い",
    ['o'] = "お",
    ['u'] = "う",
    [','] = "、",
    ['.'] = "。",
    ['/'] = "・",
    ['<'] = "＜",
    ['>'] = "＞",
    ['?'] = "？",
    ['['] = "「",
    [']'] = "」",
    ['{'] = "｛",
    ['}'] = "｝",
    ['~'] = "〜",
    ['!'] = "！",
    ['@'] = "＠",
    ['#'] = "＃",
    ['$'] = "＄",
    ['%'] = "％",
    ['^'] = "＾",
    ['&'] = "＆",
    ['*'] = "＊",
    ['('] = "（",
    [')'] = "）",
    ['+'] = "＋",
    ['`'] = "｀",
    ['1'] = "１",
    ['2'] = "２",
    ['3'] = "３",
    ['4'] = "４",
    ['5'] = "５",
    ['6'] = "６",
    ['7'] = "７",
    ['8'] = "８",
    ['9'] = "９",
    ['0'] = "０",
    ['='] = "＝",
    ['|'] = "｜",
    ['\\'] = "￥",
};

static char const *const youon[128] = {
    ['a'] = "ゃ",
    ['u'] = "ゅ",
    ['o'] = "ょ",
};

static char const *const b_pairs[128] = {
    ['a'] = "ば",
    ['e'] = "べ",
    ['i'] = "び",
    ['o'] = "ぼ",
    ['u'] = "ぶ",
};

static char const *const d_pairs[128] = {
    ['a'] = "だ",
    ['e'] = "で",
    ['i'] = "ぢ",
    ['o'] = "ど",
    ['u'] = "づ",
};

static char const *const f_pairs[128] = {
    ['a'] = "ふぁ",
    ['e'] = "ふぇ",
    ['i'] = "ふぃ",
    ['o'] = "ふぉ",
    ['u'] = "ふ",
};

static char const *const g_pairs[128] = {
    ['a'] = "が",
    ['e'] = "げ",
    ['i'] = "ぎ",
    ['o'] = "ご",
    ['u'] = "ぐ",
};

static char const *const h_pairs[128] = {
    ['a'] = "は",
    ['e'] = "へ",
    ['i'] = "ひ",
    ['o'] = "ほ",
    ['u'] = "ふ",
};

static char const *const j_pairs[128] = {
    ['a'] = "じゃ",
    ['e'] = "じぇ",
    ['i'] = "じ",
    ['o'] = "じょ",
    ['u'] = "じゅ",
};

static char const *const k_pairs[128] = {
    ['a'] = "か",
    ['e'] = "け",
    ['i'] = "き",
    ['o'] = "こ",
    ['u'] = "く",
};

static char const *const l_pairs[128] = {
    ['a'] = "ぁ",
    ['e'] = "ぇ",
    ['i'] = "ぃ",
    ['o'] = "ぉ",
    ['u'] = "ぅ",
};

static char const *const m_pairs[128] = {
    ['a'] = "ま",
    ['e'] = "め",
    ['i'] = "み",
    ['o'] = "も",
    ['u'] = "む",
};

static char const *const n_pairs[128] = {
    ['a'] = "な",
    ['e'] = "ね",
    ['i'] = "に",
    ['o'] = "の",
    ['u'] = "ぬ",
    ['n'] = "ん",

    ['b'] = "んb",
    ['d'] = "んd",
    ['f'] = "んf",
    ['g'] = "んg",
    ['h'] = "んh",
    ['j'] = "んj",
    ['k'] = "んk",
    ['l'] = "んl",
    ['m'] = "んm",
    ['p'] = "んp",
    ['r'] = "んr",
    ['s'] = "んs",
    ['t'] = "んt",
    ['v'] = "んv",
    ['w'] = "んw",
    ['x'] = "んx",
    ['y'] = "んy",
    ['z'] = "んz",
};

static char const *const p_pairs[128] = {
    ['a'] = "ぱ",
    ['e'] = "ぺ",
    ['i'] = "ぴ",
    ['o'] = "ぽ",
    ['u'] = "ぷ",
};

static char const *const r_pairs[128] = {
    ['a'] = "ら",
    ['e'] = "れ",
    ['i'] = "り",
    ['o'] = "ろ",
    ['u'] = "る",
};

static char const *const s_pairs[128] = {
    ['a'] = "さ",
    ['e'] = "せ",
    ['i'] = "し",
    ['o'] = "そ",
    ['u'] = "す",
};

static char const *const t_pairs[128] = {
    ['a'] = "た",
    ['e'] = "て",
    ['i'] = "ち",
    ['o'] = "と",
    ['u'] = "つ",
};

static char const *const v_pairs[128] = {
    ['a'] = "ゔぁ",
    ['e'] = "ゔぇ",
    ['i'] = "ゔぃ",
    ['o'] = "ゔぉ",
    ['u'] = "ゔ",
};

static char const *const w_pairs[128] = {
    ['a'] = "わ",
    ['e'] = "うぇ",
    ['i'] = "うぃ",
    ['o'] = "を",
    ['u'] = "う",
};

static char const *const x_pairs[128] = {
    ['a'] = "ぁ",
    ['e'] = "ぇ",
    ['i'] = "ぃ",
    ['o'] = "ぉ",
    ['u'] = "ぅ",
};

static char const *const y_pairs[128] = {
    ['a'] = "や",
    ['e'] = "いぇ",
    ['i'] = "い",
    ['o'] = "よ",
    ['u'] = "ゆ",
};

static char const *const z_pairs[128] = {
    ['a'] = "ざ",
    ['e'] = "ぜ",
    ['i'] = "じ",
    ['o'] = "ぞ",
    ['u'] = "ず",
};

static char const *const *const twos[128] = {
    ['b'] = b_pairs,
    ['d'] = d_pairs,
    ['f'] = f_pairs,
    ['g'] = g_pairs,
    ['h'] = h_pairs,
    ['j'] = j_pairs,
    ['k'] = k_pairs,
    ['l'] = l_pairs,
    ['m'] = m_pairs,
    ['n'] = n_pairs,
    ['p'] = p_pairs,
    ['r'] = r_pairs,
    ['s'] = s_pairs,
    ['t'] = t_pairs,
    ['v'] = v_pairs,
    ['w'] = w_pairs,
    ['x'] = x_pairs,
    ['y'] = y_pairs,
    ['z'] = z_pairs,
};

void anthywl_buffer_init(struct anthywl_buffer *buffer) {
    buffer->text = calloc(1, 1);
    buffer->len = 0;
    buffer->pos = 0;
}

void anthywl_buffer_destroy(struct anthywl_buffer *buffer) {
    free(buffer->text);
}

void anthywl_buffer_clear(struct anthywl_buffer *buffer) {
    buffer->text[0] = '\0';
    buffer->len = 0;
    buffer->pos = 0;
}

void anthywl_buffer_append(struct anthywl_buffer *buffer, char const *text) {
    size_t text_len = strlen(text);
    buffer->text = realloc(buffer->text, buffer->len + text_len + 1);
    if (buffer->pos == 0) {
        memmove(
            buffer->text + text_len,
            buffer->text,
            buffer->len + 1);
        memcpy(
            buffer->text,
            text,
            text_len);
    } else if (buffer->pos == buffer->len) {
        memcpy(
            buffer->text + buffer->len,
            text,
            text_len + 1);
    } else {
        memmove(
            buffer->text + buffer->pos + text_len,
            buffer->text + buffer->pos,
            buffer->len - buffer->pos + 1);
        memcpy(
            buffer->text + buffer->pos,
            text,
            text_len);
    }
    buffer->len += text_len;
    buffer->pos += text_len;
}

void anthywl_buffer_delete_backwards(struct anthywl_buffer *buffer, size_t amt)
{
    if (buffer->pos == 0)
        return;
    size_t end = buffer->pos;
    size_t start = buffer->pos;
    for (size_t i = 0; i < amt; i++) {
        start -= 1;
        for (; start != 0; start--) {
             if ((buffer->text[start] & 0x80) == 0
                || (buffer->text[start] & 0xC0) == 0xC0)
            {
                break;
            }
        }
    }
    memmove(
        buffer->text + start,
        buffer->text + end,
        buffer->len - end + 1);
    buffer->len -= end - start;
    buffer->pos -= end - start;
}

void anthywl_buffer_delete_forwards(struct anthywl_buffer *buffer, size_t amt) {
    if (buffer->pos == buffer->len)
        return;
    size_t start = buffer->pos;
    size_t end = start + 1;
    for (; end != buffer->len; end++) {
         if ((buffer->text[end] & 0x80) == 0
            || (buffer->text[end] & 0xC0) == 0xC0)
        {
            break;
        }
    }
    memmove(
        buffer->text + start,
        buffer->text + end,
        buffer->len - end + 1);
    buffer->len -= end - start;
}

void anthywl_buffer_move_left(struct anthywl_buffer *buffer) {
    if (buffer->pos == 0)
        return;
    buffer->pos -= 1;
    while ((buffer->text[buffer->pos] & 0x80) != 0
        && (buffer->text[buffer->pos] & 0xC0) != 0xC0)
    {
        buffer->pos -= 1;
    }
}

void anthywl_buffer_move_right(struct anthywl_buffer *buffer) {
    if (buffer->pos == buffer->len)
        return;
    buffer->pos += 1;
    while ((buffer->text[buffer->pos] & 0x80) != 0
        && (buffer->text[buffer->pos] & 0xC0) != 0xC0)
    {
        buffer->pos += 1;
    }
}

void anthywl_buffer_convert_romaji(struct anthywl_buffer *buffer) {
    if (buffer->pos == 0)
        return;

    unsigned char c0 = '\0', c1 = '\0', c2 = '\0', c3 = '\0';

    if (buffer->pos >= 1) c3 = buffer->text[buffer->pos - 1];
    if (buffer->pos >= 2) c2 = buffer->text[buffer->pos - 2];
    if (buffer->pos >= 3) c1 = buffer->text[buffer->pos - 3];
    if (buffer->pos >= 4) c0 = buffer->text[buffer->pos - 4];

    if (c3 >= 0x80) c3 = '\0';
    if (c2 >= 0x80) c2 = '\0';
    if (c1 >= 0x80) c1 = '\0';
    if (c0 >= 0x80) c0 = '\0';

    if (c2 == 'n' && c3 == '\'') {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, "ん");
        return;
    }

    if (!c3)
        return;

    if (c3 == '.') {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, "。");
    }

    if (c3 == '.') {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, "。");
    }

    if (c2 == c3 && twos[c3]) {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, "っ");
        anthywl_buffer_append(buffer, (char[]){c3, 0});
    }

    // shi -> si
    // chi -> ti
    // thi -> texi
    // t'i -> texi
    // dzu -> du

    if (c1 == 's' && c2 == 'h' && c3 == 'i') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "し");
        return;
    }

    if (c1 == 'c' && c2 == 'h' && c3 == 'i') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "ち");
        return;
    }

    if ((c1 == 'x' || c1 == 'l') && c2 == 't' && c3 == 'u') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "っ");
        return;
    }

    if (c1 == 't' && c2 == 's' && c3 == 'u') {
        if (c0 == 'x' || c0 == 'l') {
            anthywl_buffer_delete_backwards(buffer, 4);
            anthywl_buffer_append(buffer, "っ");
            return;
        }
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "つ");
        return;
    }

    if (c1 == 's' && c2 == 'h' && youon[c3])
        c2 = 'y';

    if (c1 == 'c' && c2 == 'h' && youon[c3])
        c1 = 't', c2 = 'y';

    if (c1 && c2 == 'y' && youon[c3]) {
        anthywl_buffer_delete_backwards(buffer, 3);
        if (c1 != 'x' && c1 != 'l')
            anthywl_buffer_append(buffer, twos[c1]['i']);
        anthywl_buffer_append(buffer, youon[c3]);
        return;
    }

    if (c2 && twos[c2] && twos[c2][c3]) {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, twos[c2][c3]);
        return;
    }

    if (ones[c3]) {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, ones[c3]);
        return;
    }
}

void anthywl_buffer_convert_trailing_n(struct anthywl_buffer *buffer) {
    if (buffer->pos == 0)
        return;
    if (buffer->text[buffer->pos - 1] == 'n') {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, "ん");
    }
}
