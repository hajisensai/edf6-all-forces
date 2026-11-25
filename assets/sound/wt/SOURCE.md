# War Thunder gun sounds (assets/sound/wt)

Source: https://github.com/GaijinEntertainment/fmod_studio_warthunder_for_modders , commit `07fd12efffbf20b2053d630ed5cf7ca3ddfa54a6`.
The upstream Readme says the project "allows you to create sound modifications" and that using the content means
agreeing to the War Thunder contribution agreement (https://live.warthunder.com/contribution_agreement/?lang=en).
The repository owner allowed committing the processed clips; the terms are those of the upstream repository.
The upstream assets are already vorbis-compressed (ogg renamed to .wav); they are decoded once here and written as
16-bit PCM.

Made by `tools/wt_sound.py` (clip names and variant counts: src/wt_clips.h CREW_WT_CLIPS). Per file: channels kept
(`sub`: mixed to mono), resampled to 48000 Hz, 20 Hz high-pass, no per-file levelling or normalising, start kept,
tail under -60 dBFS cut with a 20 ms fade-out; close_150 is cut to 5.0 s and tail_heavy to 7.0 s
(300 ms fade-out).
One gain for the whole set: **0.680167** (set peak before gain 1.440824; scaled only when over 0.98).

Installed to `Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav` by the installer.

Files: 92, total size 35542172 bytes (33.90 MiB).

| file | source (under the project root) | channels | seconds | bytes |
|---|---|---|---|---|
| shot_mid_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_02_shot-001.wav | 2 | 2.400 | 460844 |
| shot_mid_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_02_shot-002.wav | 2 | 2.400 | 460844 |
| shot_mid_3.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_02_shot-003.wav | 2 | 2.400 | 460844 |
| shot_big_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_01_shot-001.wav | 2 | 2.400 | 460844 |
| shot_big_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_01_shot-002.wav | 2 | 2.400 | 460844 |
| shot_big_3.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_01_shot-003.wav | 2 | 2.400 | 460844 |
| shot_heavy_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_07_shot-001.wav | 2 | 2.400 | 460844 |
| shot_heavy_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_07_shot-002.wav | 2 | 2.400 | 460844 |
| shot_heavy_3.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_07_shot-003.wav | 2 | 2.400 | 460844 |
| middle_mid_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_02_middle-001.wav | 2 | 2.312 | 444032 |
| middle_mid_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_02_middle-002.wav | 2 | 2.381 | 457244 |
| middle_big_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_01_middle-001.wav | 2 | 2.303 | 442180 |
| middle_big_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_01_middle-002.wav | 2 | 2.291 | 439884 |
| bass_1.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_05_bass-001.wav | 2 | 2.188 | 420184 |
| bass_2.wav | tanks/weapon/cannons/cannons_close_modules/close_17_12_2021/cannon_05_bass-002.wav | 2 | 2.143 | 411536 |
| body_big_1.wav | tanks/weapon/cannons/cannons_close_modules/body_big_01-001.wav | 2 | 2.803 | 538236 |
| body_big_2.wav | tanks/weapon/cannons/cannons_close_modules/body_big_01-002.wav | 2 | 2.783 | 534316 |
| metal_1.wav | tanks/weapon/cannons/cannons_close_modules/metal/metal_body_big_02-001.wav | 2 | 1.200 | 230444 |
| metal_2.wav | tanks/weapon/cannons/cannons_close_modules/metal/metal_body_big_02-002.wav | 2 | 1.200 | 230444 |
| mech_howitzer_1.wav | tanks/weapon/cannons/howitzer_shot_mech_01.wav | 2 | 3.215 | 617356 |
| mech_howitzer_2.wav | tanks/weapon/cannons/howitzer_shot_mech_02.wav | 2 | 3.185 | 611580 |
| close_75_1.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_01.wav | 2 | 4.000 | 768044 |
| close_75_2.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_02.wav | 2 | 4.000 | 768044 |
| close_75_3.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_03.wav | 2 | 4.000 | 768044 |
| close_122_1.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_01.wav | 2 | 3.801 | 729860 |
| close_122_2.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_02.wav | 2 | 3.839 | 737084 |
| close_122_3.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_03.wav | 2 | 4.000 | 768044 |
| close_128_1.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_01.wav | 2 | 3.894 | 747640 |
| close_128_2.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_02.wav | 2 | 3.659 | 702508 |
| close_128_3.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_03.wav | 2 | 3.723 | 714940 |
| close_150_1.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_close-001.wav | 2 | 4.989 | 957908 |
| close_150_2.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_close-002.wav | 2 | 5.000 | 960044 |
| close_150_3.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_close-003.wav | 2 | 5.000 | 960044 |
| far_75_1.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_far_01.wav | 1 | 3.469 | 333058 |
| far_75_2.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_far_02.wav | 1 | 3.086 | 296294 |
| far_75_3.wav | tanks/weapon/cannons/cannon_75mm_kwk40/cannon_75mm_kwk40_shot_far_03.wav | 1 | 2.902 | 278662 |
| far_122_1.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_far_01.wav | 2 | 3.711 | 712496 |
| far_122_2.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_far_02.wav | 2 | 3.681 | 706828 |
| far_122_3.wav | tanks/weapon/cannons/cannon_122mm_d25t/cannon_122mm_d25t_shot_far_03.wav | 2 | 3.782 | 726140 |
| far_128_1.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_far_01.wav | 1 | 3.581 | 343792 |
| far_128_2.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_far_02.wav | 1 | 3.393 | 325758 |
| far_128_3.wav | tanks/weapon/cannons/cannon_128mm_pak44/cannon_128mm_pak44_shot_far_03.wav | 1 | 3.687 | 354020 |
| far_150_1.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_dist-001.wav | 1 | 5.000 | 480044 |
| far_150_2.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_dist-002.wav | 1 | 4.824 | 463114 |
| far_150_3.wav | tanks/weapon/cannons/cannon_150mm_sfh_18/150mm_sfh_18_dist-003.wav | 1 | 5.000 | 480044 |
| bump_med_1.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_med_add-001.wav | 1 | 1.000 | 96044 |
| bump_med_2.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_med_add-002.wav | 1 | 1.000 | 96044 |
| bump_med_3.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_med_add-003.wav | 1 | 1.000 | 96044 |
| bump_big_1.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_big_add-001.wav | 1 | 1.500 | 144040 |
| bump_big_2.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_big_add-002.wav | 1 | 1.477 | 141858 |
| bump_big_3.wav | tanks/weapon/cannons/cannons_far/cannon_ground_far_big_add-003.wav | 1 | 1.500 | 144038 |
| sub_1.wav | tanks/weapon/cannons/cannon_common_tails/cannons_sub-001.wav | 1 | 2.500 | 240044 |
| sub_2.wav | tanks/weapon/cannons/cannon_common_tails/cannons_sub-002.wav | 1 | 2.500 | 240044 |
| sub_3.wav | tanks/weapon/cannons/cannon_common_tails/cannons_sub-003.wav | 1 | 2.500 | 240044 |
| tail_mid_1.wav | tanks/weapon/cannons/cannon_common_tails/cannon_84_94_tail_far_01.wav | 2 | 5.237 | 1005616 |
| tail_mid_2.wav | tanks/weapon/cannons/cannon_common_tails/cannon_84_94_tail_far_02.wav | 2 | 5.035 | 966808 |
| tail_big_1.wav | tanks/weapon/cannons/cannon_common_tails/cannon_100_122_tail_far_01.wav | 2 | 6.061 | 1163768 |
| tail_big_2.wav | tanks/weapon/cannons/cannon_common_tails/cannon_100_122_tail_far_02.wav | 2 | 5.065 | 972572 |
| tail_heavy_1.wav | tanks/weapon/cannons/cannon_common_tails/cannon_128_150_tail_far_01.wav | 2 | 7.000 | 1344044 |
| tail_heavy_2.wav | tanks/weapon/cannons/cannon_common_tails/cannon_128_150_tail_far_02.wav | 2 | 7.000 | 1344044 |
| m2_close_1.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-001.wav | 2 | 0.105 | 20156 |
| m2_close_2.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-002.wav | 2 | 0.105 | 20152 |
| m2_close_3.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-003.wav | 2 | 0.105 | 20156 |
| m2_close_4.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-004.wav | 2 | 0.105 | 20156 |
| m2_close_5.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-005.wav | 2 | 0.105 | 20148 |
| m2_close_6.wav | tanks/weapon/mg/oneshot/m2/grains/m2_close-006.wav | 2 | 0.105 | 20156 |
| m2_far_1.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-001.wav | 1 | 0.105 | 10102 |
| m2_far_2.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-002.wav | 1 | 0.105 | 10102 |
| m2_far_3.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-003.wav | 1 | 0.105 | 10102 |
| m2_far_4.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-004.wav | 1 | 0.105 | 10102 |
| m2_far_5.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-005.wav | 1 | 0.105 | 10100 |
| m2_far_6.wav | tanks/weapon/mg/oneshot/m2/grains/m2_far-006.wav | 1 | 0.105 | 10102 |
| m2_last_close_1.wav | tanks/weapon/mg/oneshot/m2/grains/m2_last_close-001.wav | 2 | 1.938 | 372188 |
| m2_last_close_2.wav | tanks/weapon/mg/oneshot/m2/grains/m2_last_close-002.wav | 2 | 1.875 | 360044 |
| m2_last_far_1.wav | tanks/weapon/mg/oneshot/m2/grains/m2_last_far-001.wav | 2 | 1.938 | 372188 |
| m2_last_far_2.wav | tanks/weapon/mg/oneshot/m2/grains/m2_last_far-002.wav | 2 | 1.875 | 360048 |
| mg42_close_1.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-001.wav | 2 | 0.048 | 9332 |
| mg42_close_2.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-002.wav | 2 | 0.048 | 9332 |
| mg42_close_3.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-003.wav | 2 | 0.048 | 9332 |
| mg42_close_4.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-004.wav | 2 | 0.048 | 9332 |
| mg42_close_5.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-005.wav | 2 | 0.048 | 9332 |
| mg42_close_6.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_close-006.wav | 2 | 0.048 | 9332 |
| mg42_far_1.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-001.wav | 1 | 0.048 | 4688 |
| mg42_far_2.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-002.wav | 1 | 0.048 | 4688 |
| mg42_far_3.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-003.wav | 1 | 0.048 | 4688 |
| mg42_far_4.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-004.wav | 1 | 0.048 | 4688 |
| mg42_far_5.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-005.wav | 1 | 0.048 | 4688 |
| mg42_far_6.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_far-006.wav | 1 | 0.048 | 4688 |
| mg42_last_close_1.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_last_close-001.wav | 2 | 0.799 | 153516 |
| mg42_last_close_2.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_last_close-002.wav | 2 | 0.874 | 167912 |
| mg42_last_far_1.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_last_far-001.wav | 2 | 1.650 | 316844 |
| mg42_last_far_2.wav | tanks/weapon/mg/oneshot/mg_series/grains/mg42_last_far-002.wav | 2 | 1.725 | 331240 |
