# STAR — 素材とデータの出典

## 天体・観測データ

- **地球の詳細陸地・水域分類** — © ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium。[WorldCover data access](https://esa-worldcover.org/en/data-access)、[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。Sentinel-2 L2A年間中央値RGB/NIRとv200水域分類を使用。観測年は2021年で、現在の景観・照明に依存しない完全なalbedoではありません。
- **地球の立体地形** — Copernicus DEM © DLR e.V. 2010–2014 and © Airbus Defence and Space GmbH 2014–2018。[AWS GLO-30 Public](https://registry.opendata.aws/copernicus-dem/)、2021 release、2011–2015年の観測、EGM2008正標高のDSM。公称30mとゲームの描画LOD間隔は別です。
- **天体の日時・位置と姿勢** — NASA/JPL Horizons、NAIF SPICE。2026年の幾何学的位置と天体固定座標系を補間します。元データの予測・測地基準の限界は保持し、補間残差を観測精度とは扱いません。

- **地球の昼面** — NASA Earth Observatory、Reto Stöckli、MODIS Science Team。[Blue Marble Next Generation](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-map/)の2004年9月画像を使用。
- **地球の夜景** — NASA Earth Observatory、NOAA NGDC、Suomi NPP VIIRS Team。[Black Marbleの2012年全球画像](https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/dnb_land_ocean_ice.2012.13500x6750.jpg)。現在の都市照明を示すものではありません。
- **地球の雲** — NASA Goddard Space Flight Center、Reto Stöckli、MODIS Science Team、Robert Simmon。[Blue Marbleの雲の合成画像](https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_8192.tif)。2002年公開の過去の合成画像を視覚的な雲分布として使用しています。
- **月の色と全球標高** — NASA Scientific Visualization Studio、Ernie Wright、NASA GSFC、MIT、Arizona State University、LRO LROC/LOLA。[CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/)。色画像にはNASAによる美観調整・極域の補完が含まれます。
- **Apollo 17周辺の地形** — NASA、GSFC、Arizona State University、LRO LROC Team、Mark Robinson。[LROC NAC DTM](https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/DATA/SDP/NAC_DTM/APOLLO17/NAC_DTM_APOLLO17_README.TXT)。ゲームでは5m間隔の地形データを使用。
- **土星本体** — NASA/JPL-Caltech/Space Science Institute、NASA PDS、New Mexico State University。[Cassini ISS Global Maps](https://atmos.nmsu.edu/PDS/data/PDS4/co_iss_global-maps/data_derived/)、DOI 10.17189/rkkb-6y30。2011年8月11日のRGB派生全球図を使用。欠測画素を区別し、周囲の帯の色から補っています。
- **土星の環** — NASA PDS Ring-Moon Systems Node、Cassini UVIS Team、Joshua E. Colwell。[UVISの観測プロファイル](https://pds-rings.seti.org/holdings/volumes/COUVIS_8xxx/COUVIS_8001/data/UVIS_HSP_2005_139_126TAU_E_TAU01KM.TAB)。光学的厚さ・半径方向の分布を描画へ反映し、視点に応じた散乱はゲーム側で計算しています。
- **星空** — NASA Scientific Visualization Studio、Ernie Wright、Hipparcos-2、Tycho-2、Gaia DR2と補助カタログ。[Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/)。恒星カタログに基づく背景画像です。
- **天体配置と姿勢** — NASA/JPL [Horizons](https://ssd-api.jpl.nasa.gov/doc/horizons.html)、NAIF [SPICE](https://naif.jpl.nasa.gov/naif/)。基準時刻は2026年9月19日00:00 UTC。ゲーム内の高速航行は架空の仕組みです。

## 表面の写真素材

- **操縦席のゴム・樹脂表面** — Amal Kumar、Poly Haven [Rubber Tiles](https://polyhaven.com/a/rubber_tiles)。[CC0](https://polyhaven.com/license)。継ぎ目を避けた小範囲を切り出し、25cmの反復尺度で使用。
- **月面の微細な質感** — NASA/JSC/Arizona State University。[Apollo 11 AS11-45-6705Aの未処理スキャン](https://data.lroc.im-ldi.com/data/35mm/AS11/raw/AS11-45-6705A.tif)、1969年7月20日。約38.18mmの範囲を質感の参考に使用。照明補正・法線の推定を含み、Apollo 17地点の実測形状ではありません。
- 金属の色・反射率・粗さには物理モデルによる調整も含まれます。すべての機体表面が写真計測素材という意味ではありません。

## 機体・音・ソフトウェア

- 探査船、内装、UI、ゲームプログラム：STARオリジナル。
- Unreal Engine 5.8.2 — Epic Games。Unreal Engineおよび関連商標はEpic Gamesの商標です。
- SDL 3 — Simple DirectMedia Layer、[zlibライセンス](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt)。同梱のライセンス文を参照してください。
- Noto Sans JP — Google / Noto Project、SIL Open Font License 1.1。同梱フォントのライセンス文を参照してください。

## 背景・機体表面

- **強調表示の天の川 — ESO/S. Brunier — CC BY 4.0**。[ESO0932a](https://www.eso.org/public/images/eso0932a/)。2008〜2009年の地上写真を再サンプルし、おおよその銀河座標登録と露出補正を加えています。精密な星の位置計測用ではなく、ESOによる本作の推奨・公認を意味しません。
- **機体の布・金属・コーティング** — Poly HavenのTerlenka（colormass撮影、Rico Cilliers処理）、Rubber Tiles（Amal Kumar）、Interior Tiles（Charlotte Baglioni）、ambientCGのMetal063。CC0の写真由来素材を類似材として加工し、元の機体色を保持。実際の宇宙船用材料の測定値ではありません。
- **遮熱板・断熱箔** — NASA/Johns Hopkins APLのParker Solar Probe遮熱板写真、NASA/JPLのCassini実機写真。表面変化へ加工し、法線・粗さには美術的な推定を含みます。
- **真空中の噴射** — NASA/JSC、[ISS066E125277](https://www.nasa.gov/image-article/plumes-from-spacex-cargo-dragons-draco-engines/)、2022年1月23日。Cargo Dragon Dracoエンジンの軌道上写真から噴流を抽出。形状、明るさ、動き、架空の主エンジンへの適用はゲーム用の再構成です。

取得元、観測日、色空間、座標系、単位と加工は `Data` および素材パックのprovenanceに記載しています。

## 月面・操縦席の詳細

- **地球の近景** — NASA/GSFC、MODIS Aqua、LANCE、GIBS。2025年9月6日のCorrected Reflectance合成画像を88–104°E、3–19°Nへ登録。250–500mの元観測を約217m格子へ再サンプルした表示用画像で、雲の立体形状や現在の天候ではありません。
- **月面の山と地面** — NASA/JSC、Apollo Lunar Surface Journal、Kipp Teagueの原フィルム走査資料。Apollo 17の写真をLROC実測地形へ部分的に登録。ゲームでは写真の色かぶり・広い明暗を抑え、局所的な濃淡を標準表面へ混合しています。観測アルベドへの変換ではなく、未撮影・逆光・欠測部分はマスクしています。近景の細粒や小石の配置・法線は推定表現です。LROC遠景は元DTM全域を10m格子へ再サンプルしています。
- **コックピット** — NASA/JSC [jsc2022e044970](https://images.nasa.gov/details/jsc2022e044970)（2016年5月11日のOrion mockup）を製造構造の参考とし、断熱材の一部を使用。ASTER 24は架空の機体で、NASA機の複製や公認製品ではありません。
- **鮮明な恒星** — NASA/GSFC Scientific Visualization Studio、Ernie Wright。[Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/)の16K Hipparcos/Tycho恒星図。ESO/S. Brunierの背景写真には、小さな星の重複を抑える形態学的処理とぼかしを施しています。背景の分離と表示輝度は美術的な推定です。

## 公開版の音

- 効果音・環境音楽：STARオリジナルの手続き生成音源。MITライセンス。生成ソースは `Tools/Audio`。実録音や真空中の音響の再現ではありません。

## 利用条件の参照先

- [NASA画像・メディアの利用条件](https://www.nasa.gov/nasa-brand-center/images-and-media/)
- [ESA WorldCover](https://esa-worldcover.org/en/data-access) / [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
- [Copernicus DEMの利用条件・指定表示](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM)
- [ESOの著作権表示](https://www.eso.org/public/outreach/copyright/)

produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved

STARはNASA、ESA、ESO等による公認・推奨製品ではありません。

## 補助的な地表画像

- EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017). CC BY 4.0 https://creativecommons.org/licenses/by/4.0/ 。海岸の色と水域を加工しています。
- カイロの夜景写真：Image courtesy of the Earth Science and Remote Sensing Unit, NASA Johnson Space Center. ISS069-E-37411–37414、2023年7月26日。[公開元](https://science.nasa.gov/earth/earth-observatory/cairos-colorful-nightscape-153803/)。座標登録とマスク処理を施しています。
