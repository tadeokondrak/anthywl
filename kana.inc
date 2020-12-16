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
