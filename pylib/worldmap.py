"""The world map's places (docs/plan-2026-11-10-worldmap.md §3): every game map a mission loads -> one real place on
Earth, so each mission's marker stands on a real location and the route to the next mission runs between real places.

The games never name a real location (EDF6's briefings: "city", "town", "mountains", "Base 251"; tools/make_worldmap.py
reads which map each mission loads instead). So the place is chosen from what the map *is* (its file name, romanised
Japanese: haikyo = ruins, danti = housing estate, rinkai = waterfront, kitaguni = the snow country, sangaku =
mountains, ...): a real Japanese location of that kind (the series is set in Japan). Every pairing is a design choice
(evidence L), the meaning of the map name H/M (named in the column `why`).

PLACES  place id -> Place (the names in the game's five text languages, latitude / longitude in degrees, its region).
MAPS    map name (lower case, no .mac; worldmap/missions.json) -> place id. Several maps may share a place.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

REGIONS = ('hokkaido', 'tohoku', 'kanto', 'chubu', 'kansai', 'chugoku', 'kyushu')
# The detail texture's bounds (tools/worldmap_basemap.py derive: a square crop of Blue Marble tile D1, 20 x 20 degrees,
# equirectangular): every place lies inside, so the zoomed-in view never needs the coarse world texture.
DETAIL_NORTH, DETAIL_SOUTH, DETAIL_WEST, DETAIL_EAST = 47.0, 27.0, 127.0, 147.0

# The two base map textures, the one place their paths are written (docs/plan-2026-11-10-worldmap.md §2):
# tools/worldmap_basemap.py textures builds them into the model pack's folder TEXTURE_DIR (tools/build_release.py
# RELEASE_MODELS), tools/worldmap_pack.py installs them under Mods at TEXTURES[kind], and tools/gen_worldmap.py hands
# the plugin their app:/ paths (src/worldmap.inc kWorldTexture / kJapanTexture).
TEXTURE_DIR = 'worldmap_tex'
TEXTURE_INDEX = 'worldmap_tex.json'     # in TEXTURE_DIR: {'version', 'files': {name: {'sha256', 'size'}}}
TEXTURE_VERSION = 1     # the index's version: raise when the textures' bytes change for the same sources
TEXTURES = {'world': 'UI/EDF6VC_WORLDMAP_WORLD.DDS',    # under Mods; the whole Earth, 4096 x 2048
            'japan': 'UI/EDF6VC_WORLDMAP_JAPAN.DDS'}    # DETAIL_* above, 4096 x 4096


def app_path(rel: str) -> str:
    """The game's name of a file under Mods (as src/room_ui_logic.h kWhiteTexture: app:/UI/EDF6VC_WHITE.dds)."""
    stem, ext = rel.rsplit('.', 1)
    return f'app:/{stem}.{ext.lower()}'


@dataclass(frozen=True)
class Place:
    lat: float
    lon: float
    region: str
    sc: str
    tc: str
    en: str
    ja: str
    kr: str


PLACES: dict[str, Place] = {
    'gotemba': Place(35.308, 138.934, 'chubu', 'EDF 日本总部（御殿场）', 'EDF 日本總部（御殿場）', 'EDF Japan HQ (Gotemba)',
                     'EDF日本本部（御殿場）', 'EDF 일본 본부(고텐바)'),
    'higashifuji': Place(35.335, 138.850, 'chubu', '东富士演习场', '東富士演習場', 'Higashi-Fuji Training Area',
                         '東富士演習場', '히가시후지 연습장'),
    'chiyoda': Place(35.690, 139.753, 'kanto', '东京·千代田', '東京·千代田', 'Chiyoda, Tokyo', '東京・千代田', '도쿄 지요다'),
    'shinagawa': Place(35.621, 139.739, 'kanto', '东京·品川', '東京·品川', 'Shinagawa, Tokyo', '東京・品川', '도쿄 시나가와'),
    'ikebukuro': Place(35.729, 139.711, 'kanto', '东京·池袋', '東京·池袋', 'Ikebukuro, Tokyo', '東京・池袋', '도쿄 이케부쿠로'),
    'tama': Place(35.625, 139.424, 'kanto', '多摩新城', '多摩新城', 'Tama New Town', '多摩ニュータウン', '다마 뉴타운'),
    'takashimadaira': Place(35.790, 139.661, 'kanto', '高岛平住宅区', '高島平住宅區', 'Takashimadaira Estate', '高島平団地',
                            '다카시마다이라 단지'),
    'shinjuku': Place(35.690, 139.692, 'kanto', '西新宿高楼群', '西新宿高樓群', 'Nishi-Shinjuku towers', '西新宿高層ビル街',
                      '니시신주쿠 고층 빌딩가'),
    'denenchofu': Place(35.597, 139.668, 'kanto', '田园调布', '田園調布', 'Den-en-chofu', '田園調布', '덴엔초후'),
    'omiya': Place(35.906, 139.624, 'kanto', '大宫（铁道之城）', '大宮（鐵道之城）', 'Omiya (railway town)', '大宮（鉄道の街）',
                   '오미야(철도 도시)'),
    'odaiba': Place(35.627, 139.776, 'kanto', '东京湾·台场', '東京灣·台場', 'Odaiba, Tokyo Bay', '東京湾・お台場', '도쿄만 오다이바'),
    'makuhari': Place(35.648, 140.041, 'kanto', '幕张新都心', '幕張新都心', 'Makuhari', '幕張新都心', '마쿠하리'),
    'kawasaki': Place(35.531, 139.703, 'kanto', '川崎变电所', '川崎變電所', 'Kawasaki substation', '川崎変電所', '가와사키 변전소'),
    'kanamachi': Place(35.769, 139.871, 'kanto', '金町净水厂', '金町淨水廠', 'Kanamachi water works', '金町浄水場',
                       '가나마치 정수장'),
    'sapporo': Place(43.062, 141.354, 'hokkaido', '札幌', '札幌', 'Sapporo', '札幌', '삿포로'),
    'umeda': Place(34.702, 135.496, 'kansai', '大阪·梅田地下街', '大阪·梅田地下街', 'Umeda underground, Osaka',
                   '大阪・梅田地下街', '오사카 우메다 지하상가'),
    'sakae': Place(35.170, 136.908, 'chubu', '名古屋·荣地下街', '名古屋·榮地下街', 'Sakae underground, Nagoya',
                   '名古屋・栄地下街', '나고야 사카에 지하상가'),
    'hachioji': Place(35.656, 139.324, 'kanto', '八王子丘陵', '八王子丘陵', 'Hachioji hills', '八王子丘陵', '하치오지 구릉'),
    'futako': Place(35.611, 139.627, 'kanto', '多摩川河滩', '多摩川河灘', 'Tama River banks', '多摩川河川敷', '다마강 둔치'),
    'kasai': Place(35.641, 139.861, 'kanto', '葛西亲水公园', '葛西親水公園', 'Kasai waterside park', '葛西親水公園',
                   '가사이 친수공원'),
    'honmoku': Place(35.420, 139.663, 'kanto', '横滨·本牧仓库街', '橫濱·本牧倉庫街', 'Honmoku warehouses, Yokohama',
                     '横浜・本牧倉庫街', '요코하마 혼모쿠 창고가'),
    'niigata': Place(37.850, 139.030, 'chubu', '越后平原水田', '越後平原水田', 'Echigo plain paddies', '越後平野の水田',
                     '에치고 평야 논'),
    'ichihara': Place(35.520, 140.080, 'kanto', '市原炼油厂', '市原煉油廠', 'Ichihara refinery', '市原製油所', '이치하라 정유소'),
    'kitano': Place(34.700, 135.190, 'kansai', '神户·北野异人馆街', '神戶·北野異人館街', 'Kitano, Kobe (old foreign quarter)',
                    '神戸・北野異人館街', '고베 기타노 이진칸'),
    'kyoto': Place(35.011, 135.768, 'kansai', '京都', '京都', 'Kyoto', '京都', '교토'),
    'sendai': Place(38.268, 140.870, 'tohoku', '仙台', '仙台', 'Sendai', '仙台', '센다이'),
    'namba': Place(34.666, 135.501, 'kansai', '大阪·难波', '大阪·難波', 'Namba, Osaka', '大阪・難波', '오사카 난바'),
    'fukuoka': Place(33.590, 130.402, 'kyushu', '福冈', '福岡', 'Fukuoka', '福岡', '후쿠오카'),
    'aokigahara': Place(35.470, 138.620, 'chubu', '青木原熔岩洞', '青木原熔岩洞', 'Aokigahara lava caves', '青木ヶ原の溶岩洞窟',
                        '아오키가하라 용암 동굴'),
    'akiyoshi': Place(34.230, 131.303, 'chugoku', '秋吉洞', '秋吉洞', 'Akiyoshi Cave', '秋芳洞', '아키요시동굴'),
    'ryusendo': Place(39.866, 141.800, 'tohoku', '龙泉洞', '龍泉洞', 'Ryusendo Cave', '龍泉洞', '류센도 동굴'),
    'aso': Place(32.884, 131.104, 'kyushu', '阿苏火山口', '阿蘇火山口', 'Aso crater', '阿蘇火口', '아소 분화구'),
    'tottori': Place(35.540, 134.229, 'chugoku', '鸟取沙丘', '鳥取沙丘', 'Tottori Sand Dunes', '鳥取砂丘', '돗토리 사구'),
    'tokachi': Place(42.920, 143.200, 'hokkaido', '十胜平原', '十勝平原', 'Tokachi plain', '十勝平野', '도카치 평야'),
    'nasu': Place(36.950, 140.000, 'kanto', '那须平原', '那須平原', 'Nasu plain', '那須野が原', '나스 평원'),
    'kujukuri': Place(35.540, 140.450, 'kanto', '九十九里海岸', '九十九里海岸', 'Kujukuri beach', '九十九里浜', '구주쿠리 해변'),
    'tojinbo': Place(36.238, 136.124, 'chubu', '东寻坊断崖', '東尋坊斷崖', 'Tojinbo cliffs', '東尋坊', '도진보 절벽'),
    'shirakawago': Place(36.257, 136.906, 'chubu', '白川乡山村', '白川鄉山村', 'Shirakawa-go village', '白川郷', '시라카와고 마을'),
    'kirigamine': Place(36.100, 138.200, 'chubu', '雾峰高原', '霧峰高原', 'Kirigamine plateau', '霧ヶ峰高原', '기리가미네 고원'),
    'oya': Place(36.600, 139.830, 'kanto', '大谷石采石场', '大谷石採石場', 'Oya stone quarries', '大谷石採石場', '오야 채석장'),
    'kamikochi': Place(36.249, 137.637, 'chubu', '北阿尔卑斯·上高地', '北阿爾卑斯·上高地', 'Northern Alps (Kamikochi)',
                       '北アルプス・上高地', '북알프스 가미코치'),
    'minamialps': Place(35.674, 138.239, 'chubu', '南阿尔卑斯', '南阿爾卑斯', 'Southern Alps', '南アルプス', '남알프스'),
    'hoshitoge': Place(37.100, 138.620, 'chubu', '星峠梯田', '星峠梯田', 'Hoshitoge terraces', '星峠の棚田', '호시토게 계단식 논'),
    'hakone': Place(35.232, 139.024, 'kanto', '箱根山', '箱根山', 'Hakone mountains', '箱根山', '하코네산'),
    'kurobe': Place(36.820, 137.580, 'chubu', '黑部峡谷', '黑部峽谷', 'Kurobe Gorge', '黒部峡谷', '구로베 협곡'),
}

# map -> (place, why: what the map name says; H = the name says it, M = read from its letters, L = not known)
MAPS: dict[str, tuple[str, str]] = {
    # The EDF's own rooms and bases.
    'ig_edfroom01': ('gotemba', 'EDF room (H)'), 'ig_edfroom02': ('gotemba', 'EDF room (H)'),
    'edfroom601': ('gotemba', 'EDF room (H)'), 'ng_dlc_edfroom03': ('gotemba', 'EDF room (H)'),
    'ig_edfbasement01': ('gotemba', 'EDF base basement (H)'), 'ig_base502': ('higashifuji', 'a base (H)'),
    # Cities.
    'nw_haikyo601': ('chiyoda', 'haikyo: ruins (M)'), 'nw_haikyo602': ('shinagawa', 'haikyo: ruins (M)'),
    'ig_haikyo603': ('ikebukuro', 'haikyo: ruins (M)'),
    'nw_danti01': ('tama', 'danchi: housing estate (M)'), 'nw_danti01_haikyo': ('tama', 'danchi, ruined (M)'),
    'nw_danti601': ('takashimadaira', 'danchi: housing estate (M)'),
    'nw_kousoubld01': ('shinjuku', 'kousou building: high-rises (M)'),
    'nw_kousoubld601': ('shinjuku', 'kousou building: high-rises (M)'),
    'nw_kokyujyutaku01': ('denenchofu', 'koukyuu juutaku: upmarket homes (M)'),
    'nw_traincity': ('omiya', 'train city (H)'),
    'nw_rinkai01': ('odaiba', 'rinkai: waterfront (M)'), 'nw_rinkai02': ('makuhari', 'rinkai: waterfront (M)'),
    'nw_henden': ('kawasaki', 'hendensho: substation (M)'),
    'nw_jyousuicity': ('kanamachi', 'jousui: water works (M)'),
    'nw_kitagunicity': ('sapporo', 'kitaguni: the snow country (M)'),
    'nw_tikakyoju01': ('umeda', 'chika kyojuu: living underground (M)'),
    'nw_underground01': ('sakae', 'underground (H)'),
    'nw_hillycity_light': ('hachioji', 'hilly city (H)'),
    'nw_kasenjiki01': ('futako', 'kasenjiki: river banks (M)'),
    'nw_sinsuikouen': ('kasai', 'shinsui kouen: waterside park (M)'),
    'nw_soukogai01': ('honmoku', 'soukogai: warehouse street (M)'),
    'nw_suiden': ('niigata', 'suiden: rice paddies (M)'),
    'nw_seiyu': ('ichihara', 'seiyu: oil refinery (M)'), 'nw_seiyu_night': ('ichihara', 'seiyu: oil refinery (M)'),
    'nw_europe01': ('kitano', 'a European town (H): the old foreign quarter of a Japanese port'),
    'nw_dlcmap601': ('kyoto', 'a mission pack map, kind not read (L)'),
    'nw_dlcmap602': ('sendai', 'a mission pack map, kind not read (L)'),
    'ig_2000mcity': ('namba', 'a 2000 m city (H)'),
    'ig_wrecktown01': ('fukuoka', 'wreck town (H)'),
    # Caves.
    'ig_cave601_2': ('aokigahara', 'cave (H)'), 'ig_cave602_2': ('aokigahara', 'cave (H)'),
    'ig_cave603': ('aokigahara', 'cave (H)'), 'ig_cave604_2': ('aokigahara', 'cave (H)'),
    'ig_cave605': ('aokigahara', 'cave (H)'),
    'ig_cave01': ('akiyoshi', 'cave (H)'), 'ig_cave501': ('akiyoshi', 'cave (H)'), 'ig_cave503': ('akiyoshi', 'cave (H)'),
    'ig_cave504': ('akiyoshi', 'cave (H)'),
    'ig_hornetcave01': ('ryusendo', 'a hornet cave (H)'),
    # Open country.
    'ig_crater601': ('aso', 'crater (H)'), 'sk_crater01': ('aso', 'crater (H)'),
    'ig_desert508': ('tottori', 'desert (H)'),
    'ig_heigen507': ('tokachi', 'heigen: plain (M)'), 'sk_heigen02': ('tokachi', 'heigen: plain (M)'),
    'ig_heigen601': ('nasu', 'heigen: plain (M)'),
    'ig_kaigan502': ('kujukuri', 'kaigan: coast (M)'), 'sk_kaigan01': ('kujukuri', 'kaigan: coast (M)'),
    'ig_steepcoast': ('tojinbo', 'steep coast (H)'),
    'ig_mountainvillage': ('shirakawago', 'mountain village (H)'),
    'ig_plateau': ('kirigamine', 'plateau (H)'),
    'ig_rockmaze01': ('oya', 'rock maze (H)'),
    'ig_sangaku506': ('kamikochi', 'sangaku: mountains (M)'), 'sk_sangaku01': ('kamikochi', 'sangaku: mountains (M)'),
    'sk_sangaku02': ('minamialps', 'sangaku: mountains (M)'),
    'ig_terracemountain': ('hoshitoge', 'terraced mountain (H)'),
    'sk_stepmountain01': ('hakone', 'step mountain (H)'),
    'sk_valley01': ('kurobe', 'valley (H)'),
}

# The plugin's own test range pack (tools/make_edf5_campaign.py RANGE_ROW / PVP_ROWS: key prefix EDF6/EDF6TR_): its
# rows load no stock mission script, they stand at the EDF's training ground.
RANGE_KEY_PREFIX = 'EDF6/EDF6TR_'
RANGE_PLACE = 'higashifuji'


def place_of_map(name: str) -> str:
    """The place id of a map (KeyError: a map no row of MAPS names; tests/worldmap_data_test.py keeps them all named)."""
    return MAPS[name.lower().removesuffix('.mac')][0]


def project(lat: float, lon: float) -> tuple[float, float]:
    """Equirectangular (plate carree, the Blue Marble's own projection): (u, v) in [0, 1), u east from 180 W, v south
    from 90 N (src/worldmap_logic.h Project)."""
    return ((lon + 180.0) / 360.0) % 1.0, (90.0 - lat) / 180.0


def great_circle(a: tuple[float, float], b: tuple[float, float], n: int) -> list[tuple[float, float]]:
    """n >= 2 points (lat, lon) along the shorter great circle from a to b, both ends included
    (src/worldmap_logic.h GreatCircle)."""
    def vec(p: tuple[float, float]) -> tuple[float, float, float]:
        la, lo = math.radians(p[0]), math.radians(p[1])
        return math.cos(la) * math.cos(lo), math.cos(la) * math.sin(lo), math.sin(la)
    va, vb = vec(a), vec(b)
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(va, vb))))
    w = math.acos(dot)
    out = []
    for i in range(n):
        t = i / (n - 1)
        if w < 1e-9:
            v = va
        else:
            s0, s1 = math.sin((1 - t) * w) / math.sin(w), math.sin(t * w) / math.sin(w)
            v = tuple(s0 * x + s1 * y for x, y in zip(va, vb))
        out.append((math.degrees(math.atan2(v[2], math.hypot(v[0], v[1]))), math.degrees(math.atan2(v[1], v[0]))))
    return out
