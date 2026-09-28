"""Static tables for jpy_election's data builder: prefectures, PR blocs,
parties, romanisation (Hepburn) and a small shinjitai -> traditional
Chinese character map for zh-TW labels."""
import os
import re
import unicodedata

PREFS = [
    ("01", "北海道", "北海道", "Hokkaido"), ("02", "青森県", "青森", "Aomori"), ("03", "岩手県", "岩手", "Iwate"),
    ("04", "宮城県", "宮城", "Miyagi"), ("05", "秋田県", "秋田", "Akita"), ("06", "山形県", "山形", "Yamagata"),
    ("07", "福島県", "福島", "Fukushima"), ("08", "茨城県", "茨城", "Ibaraki"), ("09", "栃木県", "栃木", "Tochigi"),
    ("10", "群馬県", "群馬", "Gunma"), ("11", "埼玉県", "埼玉", "Saitama"), ("12", "千葉県", "千葉", "Chiba"),
    ("13", "東京都", "東京", "Tokyo"), ("14", "神奈川県", "神奈川", "Kanagawa"), ("15", "新潟県", "新潟", "Niigata"),
    ("16", "富山県", "富山", "Toyama"), ("17", "石川県", "石川", "Ishikawa"), ("18", "福井県", "福井", "Fukui"),
    ("19", "山梨県", "山梨", "Yamanashi"), ("20", "長野県", "長野", "Nagano"), ("21", "岐阜県", "岐阜", "Gifu"),
    ("22", "静岡県", "静岡", "Shizuoka"), ("23", "愛知県", "愛知", "Aichi"), ("24", "三重県", "三重", "Mie"),
    ("25", "滋賀県", "滋賀", "Shiga"), ("26", "京都府", "京都", "Kyoto"), ("27", "大阪府", "大阪", "Osaka"),
    ("28", "兵庫県", "兵庫", "Hyogo"), ("29", "奈良県", "奈良", "Nara"), ("30", "和歌山県", "和歌山", "Wakayama"),
    ("31", "鳥取県", "鳥取", "Tottori"), ("32", "島根県", "島根", "Shimane"), ("33", "岡山県", "岡山", "Okayama"),
    ("34", "広島県", "広島", "Hiroshima"), ("35", "山口県", "山口", "Yamaguchi"), ("36", "徳島県", "徳島", "Tokushima"),
    ("37", "香川県", "香川", "Kagawa"), ("38", "愛媛県", "愛媛", "Ehime"), ("39", "高知県", "高知", "Kochi"),
    ("40", "福岡県", "福岡", "Fukuoka"), ("41", "佐賀県", "佐賀", "Saga"), ("42", "長崎県", "長崎", "Nagasaki"),
    ("43", "熊本県", "熊本", "Kumamoto"), ("44", "大分県", "大分", "Oita"), ("45", "宮崎県", "宮崎", "Miyazaki"),
    ("46", "鹿児島県", "鹿児島", "Kagoshima"), ("47", "沖縄県", "沖縄", "Okinawa"),
]
PREF_BY_CODE = {c: {"code": c, "ja": ja, "short": s, "en": en} for c, ja, s, en in PREFS}
PREF_BY_JA = {ja: PREF_BY_CODE[c] for c, ja, s, en in PREFS}

# The 11 proportional-representation blocs (2022 apportionment, 176 seats).
BLOCS = [
    ("hokkaido", "北海道選挙区", "北海道ブロック", "Hokkaido", "北海道", 8, ["01"]),
    ("tohoku", "東北選挙区", "東北ブロック", "Tohoku", "東北", 12, ["02", "03", "04", "05", "06", "07"]),
    ("kita-kanto", "北関東選挙区", "北関東ブロック", "Northern Kanto", "北關東", 19, ["08", "09", "10", "11"]),
    ("minami-kanto", "南関東選挙区", "南関東ブロック", "Southern Kanto", "南關東", 23, ["12", "14", "19"]),
    ("tokyo", "東京都選挙区", "東京ブロック", "Tokyo", "東京", 19, ["13"]),
    ("hokuriku-shinetsu", "北陸信越選挙区", "北陸信越ブロック", "Hokuriku-Shinetsu", "北陸信越", 10,
     ["15", "16", "17", "18", "20"]),
    ("tokai", "東海選挙区", "東海ブロック", "Tokai", "東海", 21, ["21", "22", "23", "24"]),
    ("kinki", "近畿選挙区", "近畿ブロック", "Kinki", "近畿", 28, ["25", "26", "27", "28", "29", "30"]),
    ("chugoku", "中国選挙区", "中国ブロック", "Chugoku", "中國", 10, ["31", "32", "33", "34", "35"]),
    ("shikoku", "四国選挙区", "四国ブロック", "Shikoku", "四國", 6, ["36", "37", "38", "39"]),
    ("kyushu", "九州選挙区", "九州ブロック", "Kyushu", "九州", 20, ["40", "41", "42", "43", "44", "45", "46", "47"]),
]
BLOC_BY_JA = {b[1]: {"id": b[0], "label": b[1], "ja": b[2], "en": b[3], "zh": b[4], "seats": b[5], "prefs": b[6]}
              for b in BLOCS}
BLOC_BY_ID = {v["id"]: v for v in BLOC_BY_JA.values()}

# Parties contesting the 51st general election (2026-02-08).
# code, ja, short ja, en, short en, zh-TW, short zh, colour
PARTIES = [
    ("LDP", "自由民主党", "自民", "Liberal Democratic Party", "LDP", "自由民主黨", "自民黨", "#D7263D"),
    ("CRA", "中道改革連合", "中道", "Centrist Reform Alliance", "CRA", "中道改革聯合", "中道改革", "#1F5FAE"),
    ("JIP", "日本維新の会", "維新", "Japan Innovation Party (Ishin)", "Ishin", "日本維新會", "維新會", "#6FB52C"),
    ("DPFP", "国民民主党", "国民", "Democratic Party for the People", "DPP", "國民民主黨", "國民民主", "#F3B300"),
    ("SANSEI", "参政党", "参政", "Sanseito", "Sanseito", "參政黨", "參政黨", "#F07F1A"),
    ("JCP", "日本共産党", "共産", "Japanese Communist Party", "JCP", "日本共產黨", "共產黨", "#8E3E9E"),
    ("REIWA", "れいわ新選組", "れいわ", "Reiwa Shinsengumi", "Reiwa", "令和新選組", "令和", "#E4007F"),
    ("GENZEI", "減税日本・ゆうこく連合", "減ゆ", "Tax Cuts Japan & Yukoku Alliance", "Genzei-Yukoku",
     "減稅日本・憂國聯合", "減稅憂國", "#8D6E4F"),
    ("CPJ", "日本保守党", "保守", "Conservative Party of Japan", "CPJ", "日本保守黨", "保守黨", "#2E3A87"),
    ("SDP", "社会民主党", "社民", "Social Democratic Party", "SDP", "社會民主黨", "社民黨", "#1CA9E9"),
    ("MIRAI", "チームみらい", "みらい", "Team Mirai", "Mirai", "未來團隊", "未來團隊", "#00A7A0"),
    ("EUTH", "安楽死制度を考える会", "安楽死", "Society to Consider Euthanasia", "Euthanasia", "思考安樂死制度會",
     "安樂死會", "#9E9E9E"),
    ("SAISEI", "再生の道", "再生", "Path to Rebirth (Saisei no Michi)", "Saisei", "再生之道", "再生之道", "#5C6BC0"),
    # Minor parties that only contested single-member districts.
    ("MUREN", "無所属連合", "無連", "Independents' Union", "Ind. Union", "無黨籍聯合", "無黨聯合", "#90A4AE"),
    ("YAMATO", "日本大和党", "大和", "Japan Yamato Party", "Yamato", "日本大和黨", "大和黨", "#7E6B5A"),
    ("WPP", "世界平和党", "世平", "World Peace Party", "World Peace", "世界和平黨", "世界和平黨", "#81A88F"),
    ("ICHIBAN", "一番星", "一番星", "Ichibanboshi", "Ichibanboshi", "一番星", "一番星", "#B39DDB"),
    ("FUSION", "核融合党", "核融合", "Nuclear Fusion Party", "Fusion", "核融合黨", "核融合黨", "#4DB6AC"),
    ("MIRASHIN", "未来進歩党", "未進", "Future Progress Party", "Fut. Progress", "未來進步黨", "未來進步", "#A5D6A7"),
    ("JFP", "日本自由党", "日自", "Japan Liberty Party", "Liberty", "日本自由黨", "日本自由黨", "#9FA8DA"),
    ("KOKORO", "心の党", "心", "Kokoro Party", "Kokoro", "心之黨", "心之黨", "#F48FB1"),
    ("MINOR", "諸派", "諸派", "Minor parties", "Minor", "其他政黨", "其他", "#A1887F"),
    ("IND", "無所属", "無所属", "Independent", "Ind.", "無黨籍", "無黨籍", "#8C8C8C"),
]
PARTY_BY_JA = {p[1]: p for p in PARTIES}
PARTY_BY_CODE = {p[0]: p for p in PARTIES}

# Parties of the 2024 (50th) election that merged before the 2026 vote:
# the CDP and Komeito lower-house members formed the Centrist Reform
# Alliance (中道改革連合) in January 2026.
PARTY_2024 = {
    "立憲民主党": "CRA", "公明党": "CRA", "自由民主党": "LDP", "日本維新の会": "JIP", "国民民主党": "DPFP",
    "れいわ新選組": "REIWA", "日本共産党": "JCP", "参政党": "SANSEI", "日本保守党": "CPJ", "社会民主党": "SDP",
    "無所属": "IND",
}

# Justices subject to the 27th national review (国民審査), 2026-02-08.
REVIEW_JUSTICES = [
    ("takasu", "高須順一", "Junichi Takasu", "高須順一", "弁護士出身（2025年就任）", "Former attorney (appointed 2025)",
     "律師出身（2025年就任）"),
    ("okino", "沖野眞已", "Masami Okino", "沖野眞已", "学者出身（2025年就任）", "Former law professor (appointed 2025)",
     "學者出身（2025年就任）"),
]

# Counting-district names that do not match a municipality name directly.
UNIT_ALIASES = {}

# --------------------------------------------------------------------------
# Chinese (zh-TW) labels
# --------------------------------------------------------------------------
_ZH = str.maketrans({
    "県": "縣", "区": "區", "広": "廣", "沢": "澤", "浜": "濱", "児": "兒", "塩": "鹽", "関": "關", "豊": "豐",
    "条": "條", "竜": "龍", "滝": "瀧", "桜": "櫻", "横": "橫", "歳": "歲", "郷": "鄉", "恵": "惠", "仏": "佛",
    "与": "與", "万": "萬", "来": "來", "対": "對", "当": "當", "総": "總", "伝": "傳", "旧": "舊", "気": "氣",
    "楽": "樂", "薬": "藥", "発": "發", "帯": "帶", "国": "國", "駅": "驛", "黒": "黑", "亀": "龜", "宝": "寶",
    "読": "讀", "売": "賣", "円": "圓", "図": "圖", "団": "團", "声": "聲", "変": "變", "実": "實", "会": "會",
    "芸": "藝", "学": "學", "栄": "榮", "営": "營", "蔵": "藏", "徳": "德", "静": "靜", "経": "經", "霊": "靈",
    "礼": "禮", "壱": "壹", "渋": "澀", "稲": "稻", "焼": "燒", "糸": "絲", "巻": "卷", "満": "滿", "湾": "灣",
    "辺": "邊", "峡": "峽", "縄": "繩", "覇": "霸", "歯": "齒", "両": "兩", "観": "觀", "権": "權", "剣": "劍",
    "軽": "輕", "渓": "溪", "黄": "黃", "奥": "奧", "単": "單", "断": "斷", "担": "擔", "仮": "假", "価": "價",
    "画": "畫", "絵": "繪", "懐": "懷", "拡": "擴", "覚": "覺", "勧": "勸", "党": "黨", "民": "民", "進": "進",
    "連": "連", "改": "改", "維": "維", "新": "新", "参": "參", "共": "共", "産": "產", "税": "稅", "選": "選",
    "組": "組", "社": "社", "保": "保", "守": "守", "無": "無", "所": "所", "属": "屬", "増": "增", "減": "減",
    "衆": "眾", "議": "議", "院": "院", "挙": "舉", "審": "審", "査": "查", "裁": "裁", "判": "判", "訳": "譯",
    "転": "轉", "鉄": "鐵", "駒": "駒", "様": "樣", "専": "專", "険": "險", "検": "檢", "験": "驗", "続": "續",
    "継": "繼", "鶏": "雞", "圧": "壓", "囲": "圍", "医": "醫", "為": "為", "隠": "隱", "駆": "驅", "区": "區",
    "径": "徑", "茎": "莖", "県": "縣", "倹": "儉", "献": "獻", "厳": "嚴", "広": "廣", "恒": "恆", "鉱": "礦",
    "号": "號", "済": "濟", "斎": "齋", "剤": "劑", "雑": "雜", "蚕": "蠶", "惨": "慘", "残": "殘", "糸": "絲",
    "歯": "齒", "辞": "辭", "湿": "濕", "舎": "舍", "寿": "壽", "収": "收", "従": "從", "渋": "澀", "獣": "獸",
    "縦": "縱", "粛": "肅", "処": "處", "称": "稱", "証": "證", "奨": "獎", "焼": "燒", "条": "條", "状": "狀",
    "乗": "乘", "浄": "淨", "剰": "剩", "嬢": "孃", "譲": "讓", "醸": "釀", "触": "觸", "寝": "寢", "慎": "慎",
    "尽": "盡", "図": "圖", "粋": "粹", "酔": "醉", "随": "隨", "髄": "髓", "枢": "樞", "数": "數", "瀬": "瀨",
    "声": "聲", "静": "靜", "窃": "竊", "摂": "攝", "専": "專", "浅": "淺", "戦": "戰", "践": "踐", "銭": "錢",
    "潜": "潛", "繊": "纖", "禅": "禪", "双": "雙", "争": "爭", "壮": "壯", "捜": "搜", "挿": "插", "巣": "巢",
    "荘": "莊", "装": "裝", "騒": "騷", "蔵": "藏", "臓": "臟", "属": "屬", "堕": "墮", "体": "體", "対": "對",
    "滞": "滯", "台": "臺", "滝": "瀧", "択": "擇", "沢": "澤", "脱": "脫", "担": "擔", "胆": "膽", "昼": "晝",
    "鋳": "鑄", "庁": "廳", "聴": "聽", "懲": "懲", "鎮": "鎮", "逓": "遞", "鉄": "鐵", "点": "點", "転": "轉",
    "伝": "傳", "灯": "燈", "当": "當", "党": "黨", "盗": "盜", "稲": "稻", "闘": "鬥", "徳": "德", "独": "獨",
    "読": "讀", "届": "屆", "縄": "繩", "弐": "貳", "悩": "惱", "脳": "腦", "廃": "廢", "拝": "拜", "売": "賣",
    "麦": "麥", "発": "發", "髪": "髮", "抜": "拔", "繁": "繁", "晩": "晚", "蛮": "蠻", "卑": "卑", "秘": "祕",
    "浜": "濱", "賓": "賓", "頻": "頻", "敏": "敏", "瓶": "瓶", "侮": "侮", "福": "福", "払": "拂", "仏": "佛",
    "併": "併", "並": "並", "塀": "塀", "辺": "邊", "変": "變", "弁": "辯", "舗": "鋪", "歩": "步", "穂": "穗",
    "宝": "寶", "豊": "豐", "没": "沒", "翻": "翻", "毎": "每", "万": "萬", "満": "滿", "免": "免", "黙": "默",
    "弥": "彌", "訳": "譯", "薬": "藥", "与": "與", "予": "預", "余": "餘", "揺": "搖", "様": "樣", "謡": "謠",
    "来": "來", "頼": "賴", "乱": "亂", "覧": "覽", "竜": "龍", "両": "兩", "猟": "獵", "緑": "綠", "塁": "壘",
    "涙": "淚", "励": "勵", "礼": "禮", "隷": "隸", "霊": "靈", "齢": "齡", "恋": "戀", "炉": "爐", "労": "勞",
    "楼": "樓", "録": "錄", "湾": "灣", "亜": "亞", "悪": "惡", "桜": "櫻", "穏": "穩", "応": "應", "欧": "歐",
    "殴": "毆", "温": "溫", "仮": "假", "価": "價", "禍": "禍", "壊": "壞", "懐": "懷", "拡": "擴", "殻": "殼",
    "覚": "覺", "学": "學", "岳": "岳", "楽": "樂", "渇": "渴", "勧": "勸", "巻": "卷", "寛": "寬", "歓": "歡",
    "缶": "罐", "観": "觀", "陥": "陷", "関": "關", "館": "館", "気": "氣", "帰": "歸", "既": "既", "偽": "偽",
    "犠": "犧", "旧": "舊", "拠": "據", "挙": "舉", "峡": "峽", "挟": "挾", "狭": "狹", "暁": "曉", "区": "區",
    "駆": "驅", "勲": "勳", "径": "徑", "恵": "惠", "掲": "揭", "渓": "溪", "経": "經", "蛍": "螢", "軽": "輕",
    "継": "繼", "鶏": "雞", "芸": "藝", "撃": "擊", "欠": "缺", "県": "縣", "倹": "儉", "剣": "劍", "険": "險",
    "圏": "圈", "検": "檢", "権": "權", "献": "獻", "顕": "顯", "験": "驗", "厳": "嚴", "広": "廣", "効": "效",
    "黄": "黃", "国": "國", "黒": "黑", "砕": "碎", "斉": "齊", "歳": "歲", "参": "參", "桟": "棧", "蚕": "蠶",
})


def to_zh(s):
    return s.translate(_ZH) if s else s


# --------------------------------------------------------------------------
# Hepburn romanisation of katakana/hiragana
# --------------------------------------------------------------------------
_KANA = {
    "キャ": "kya", "キュ": "kyu", "キョ": "kyo", "シャ": "sha", "シュ": "shu", "ショ": "sho", "チャ": "cha",
    "チュ": "chu", "チョ": "cho", "ニャ": "nya", "ニュ": "nyu", "ニョ": "nyo", "ヒャ": "hya", "ヒュ": "hyu",
    "ヒョ": "hyo", "ミャ": "mya", "ミュ": "myu", "ミョ": "myo", "リャ": "rya", "リュ": "ryu", "リョ": "ryo",
    "ギャ": "gya", "ギュ": "gyu", "ギョ": "gyo", "ジャ": "ja", "ジュ": "ju", "ジョ": "jo", "ビャ": "bya",
    "ビュ": "byu", "ビョ": "byo", "ピャ": "pya", "ピュ": "pyu", "ピョ": "pyo", "ヂャ": "ja", "ヂュ": "ju",
    "ヂョ": "jo", "シェ": "she", "ジェ": "je", "チェ": "che", "ティ": "ti", "ディ": "di", "ファ": "fa",
    "フィ": "fi", "フェ": "fe", "フォ": "fo", "ウィ": "wi", "ウェ": "we", "ヴァ": "va", "ヴィ": "vi",
    "ア": "a", "イ": "i", "ウ": "u", "エ": "e", "オ": "o", "カ": "ka", "キ": "ki", "ク": "ku", "ケ": "ke",
    "コ": "ko", "サ": "sa", "シ": "shi", "ス": "su", "セ": "se", "ソ": "so", "タ": "ta", "チ": "chi", "ツ": "tsu",
    "テ": "te", "ト": "to", "ナ": "na", "ニ": "ni", "ヌ": "nu", "ネ": "ne", "ノ": "no", "ハ": "ha", "ヒ": "hi",
    "フ": "fu", "ヘ": "he", "ホ": "ho", "マ": "ma", "ミ": "mi", "ム": "mu", "メ": "me", "モ": "mo", "ヤ": "ya",
    "ユ": "yu", "ヨ": "yo", "ラ": "ra", "リ": "ri", "ル": "ru", "レ": "re", "ロ": "ro", "ワ": "wa", "ヰ": "i",
    "ヱ": "e", "ヲ": "o", "ン": "n", "ガ": "ga", "ギ": "gi", "グ": "gu", "ゲ": "ge", "ゴ": "go", "ザ": "za",
    "ジ": "ji", "ズ": "zu", "ゼ": "ze", "ゾ": "zo", "ダ": "da", "ヂ": "ji", "ヅ": "zu", "デ": "de", "ド": "do",
    "バ": "ba", "ビ": "bi", "ブ": "bu", "ベ": "be", "ボ": "bo", "パ": "pa", "ピ": "pi", "プ": "pu", "ペ": "pe",
    "ポ": "po", "ヴ": "vu", "ァ": "a", "ィ": "i", "ゥ": "u", "ェ": "e", "ォ": "o", "ャ": "ya", "ュ": "yu",
    "ョ": "yo", "ヮ": "wa",
}


def hira_to_kata(s):
    return "".join(chr(ord(c) + 0x60) if "ぁ" <= c <= "ゖ" else c for c in s)


def romaji(kana, simplify_long=True):
    s = hira_to_kata(unicodedata.normalize("NFKC", kana))
    out = []
    i = 0
    sokuon = False
    while i < len(s):
        c = s[i]
        if c == "ッ":
            sokuon = True
            i += 1
            continue
        if c == "ー":
            i += 1
            continue
        two = s[i:i + 2]
        r = _KANA.get(two) if len(two) == 2 else None
        if r:
            i += 2
        else:
            r = _KANA.get(c)
            i += 1
            if r is None:
                out.append(c)
                sokuon = False
                continue
        if sokuon:
            r = ("t" + r) if r.startswith("ch") else (r[0] + r)
            sokuon = False
        out.append(r)
    text = "".join(out)
    if simplify_long:
        text = text.replace("ou", "o").replace("oo", "o").replace("uu", "u")
    return text


def cap(s):
    return s[:1].upper() + s[1:] if s else s


# --------------------------------------------------------------------------
# Municipality English names from the MIC local-government code list
# --------------------------------------------------------------------------
_JIS = None


def _load_jis():
    global _JIS
    if _JIS is not None:
        return _JIS
    import openpyxl

    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "raw", "jis_codes.xlsx")
    _JIS = {}
    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    for ws in wb.worksheets:
        for row in ws.iter_rows(min_row=2, values_only=True):
            if not row or not row[0] or not row[2]:
                continue
            code = str(row[0])[:5]
            _JIS[code] = (row[2], unicodedata.normalize("NFKC", row[4] or ""))
    return _JIS


_SUFFIX = [("市", ["シ"], ""), ("区", ["ク"], "-ku"), ("町", ["チョウ", "マチ"], None), ("村", ["ムラ", "ソン"], None)]


def _base_en(name, kana):
    for suf, readings, en in _SUFFIX:
        if name.endswith(suf):
            for rd in readings:
                if kana.endswith(rd):
                    base = cap(romaji(kana[: -len(rd)]))
                    if en is None:
                        en2 = {"チョウ": "-cho", "マチ": "-machi", "ムラ": "-mura", "ソン": "-son"}[rd]
                        return base + en2
                    return base + en
    return cap(romaji(kana))


def muni_en(code, ja):
    jis = _load_jis()
    ent = jis.get(code)
    if not ent:
        return ja
    name, kana = ent
    city_code = None
    # Ward of a designated city: "札幌市中央区" with the city's own reading as prefix.
    m = re.match(r"^(.+?市)(.+区)$", name)
    if m:
        for c, (n2, k2) in jis.items():
            if n2 == m.group(1) and c[:2] == code[:2]:
                city_code = c
                break
    if city_code:
        cname, ckana = jis[city_code]
        ward_kana = kana[len(ckana):] if kana.startswith(ckana) else kana
        return f"{_base_en(cname, ckana)} {_base_en(m.group(2), ward_kana)}"
    if code.startswith("13") and name.endswith("区"):
        # Tokyo special wards are cities in their own right.
        return _base_en(name, kana).replace("-ku", "")
    return _base_en(name, kana)
