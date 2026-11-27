# War Thunder gun, explosion, passby and engine sounds (assets/sound/wt)

Source: https://github.com/GaijinEntertainment/fmod_studio_warthunder_for_modders , commit `07fd12efffbf20b2053d630ed5cf7ca3ddfa54a6`.
The upstream Readme says the project "allows you to create sound modifications" and that using the content means
agreeing to the War Thunder contribution agreement (https://live.warthunder.com/contribution_agreement/?lang=en).
The repository owner allowed committing the processed clips; the terms are those of the upstream repository.
The upstream assets are already vorbis-compressed (ogg renamed to .wav); they are decoded once here and written as
16-bit PCM.

Made by `tools/wt_sound.py` (clip names and variant counts: src/wt_clips.h CREW_WT_CLIPS). Per file: channels kept
(`art_attack_far`, `bomb_far`, `bullet_whiz`, `bypass_big`, `bypass_bullet`, `crack_big`, `crack_bullet`, `crack_mid`, `crack_small`, `dull_med`, `dull_small`, `ex_attack_far`, `ex_sub`, `hit_big`, `hit_big_far`, `hit_bullet`, `hit_med_far`, `hit_mid`, `hit_pierce`, `hit_small`, `hit_small_far`, `reload_close`, `reload_eject`, `reload_insert`, `reload_open`, `rico_hit`, `rico_whiz`, `sub`, `turret_loop`, `turret_stop`, `whiz`, `whiz_big`, `whiz_med`, `whiz_small`, `xs_far`: mixed to mono), resampled to 48000 Hz, 20 Hz high-pass, no
per-file levelling or normalising, start kept, tail under -60 dBFS cut with a 20 ms fade-out
(loops kept whole: `turret_loop`);
cut to a length (300 ms fade-out): close_150 5.0 s, tail_heavy 7.0 s, crack_big 2.5 s, bypass_big 2.0 s, hit_small 2.0 s, hit_mid 2.0 s, hit_big 2.5 s, hit_pierce 2.0 s, hit_small_far 2.0 s, hit_med_far 2.0 s, hit_big_far 2.5 s, rico_hit 1.5 s, rico_whiz 2.0 s.
One gain for the whole set: **0.680167** (set peak before gain 1.440824; scaled only when over 0.98).

Installed to `Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav` by the installer.

Files: 313, total size 124122174 bytes (118.37 MiB).

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
| xs_close_1.wav | explosion/modules/small_hits/hit_explosion_small_close_rapid-001.wav | 2 | 1.000 | 192044 |
| xs_close_2.wav | explosion/modules/small_hits/hit_explosion_small_close_rapid-002.wav | 2 | 1.000 | 192044 |
| xs_close_3.wav | explosion/modules/small_hits/hit_explosion_small_close_rapid-003.wav | 2 | 1.000 | 192044 |
| xs_far_1.wav | explosion/bullethit_far_1.wav | 1 | 2.200 | 211272 |
| xs_far_2.wav | explosion/bullethit_far_2.wav | 1 | 2.428 | 233092 |
| xs_far_3.wav | explosion/bullethit_far_3.wav | 1 | 1.806 | 173450 |
| ex_sweet_1.wav | explosive/aa_explosion_01_s.wav | 2 | 2.498 | 479660 |
| ex_sweet_2.wav | explosive/aa_explosion_02_s.wav | 2 | 3.459 | 664256 |
| ex_sweet_3.wav | explosive/aa_explosion_03_s.wav | 2 | 2.558 | 491256 |
| ex_attack_1.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-001.wav | 2 | 3.595 | 690216 |
| ex_attack_2.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-002.wav | 2 | 3.595 | 690216 |
| ex_attack_3.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-003.wav | 2 | 3.595 | 690216 |
| ex_attack_far_1.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-001_far.wav | 1 | 2.500 | 240044 |
| ex_attack_far_2.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-002_far.wav | 1 | 2.500 | 240044 |
| ex_attack_far_3.wav | explosion/modules/attack/heavy/heavy_explosion_attack_3-003_far.wav | 1 | 2.500 | 240044 |
| ex_body_1.wav | explosion/modules/layers_merge/expl_body_med-001.wav | 2 | 2.500 | 480044 |
| ex_body_2.wav | explosion/modules/layers_merge/expl_body_med-002.wav | 2 | 2.500 | 480044 |
| ex_body_3.wav | explosion/modules/layers_merge/expl_body_med-003.wav | 2 | 2.500 | 480044 |
| art_attack_1.wav | explosion/modules/layers_merge/art_attack-001.wav | 2 | 2.238 | 429692 |
| art_attack_2.wav | explosion/modules/layers_merge/art_attack-002.wav | 2 | 2.213 | 424852 |
| art_attack_3.wav | explosion/modules/layers_merge/art_attack-003.wav | 2 | 2.181 | 418844 |
| art_attack_far_1.wav | explosion/modules/layers_merge/art_attack-001_far.wav | 1 | 2.238 | 214868 |
| art_attack_far_2.wav | explosion/modules/layers_merge/art_attack-002_far.wav | 1 | 2.213 | 212448 |
| art_attack_far_3.wav | explosion/modules/layers_merge/art_attack-003_far.wav | 1 | 2.237 | 214840 |
| art_body_1.wav | explosion/modules/layers_merge/art_body-001.wav | 2 | 2.000 | 384044 |
| art_body_2.wav | explosion/modules/layers_merge/art_body-002.wav | 2 | 1.884 | 361748 |
| art_body_3.wav | explosion/modules/layers_merge/art_body-003.wav | 2 | 1.913 | 367348 |
| bomb_body_1.wav | explosion/modules/art/slow/bombs_add/slow_attack_big-001.wav | 2 | 5.500 | 1056044 |
| bomb_body_2.wav | explosion/modules/art/slow/bombs_add/slow_attack_big-002.wav | 2 | 5.500 | 1056044 |
| bomb_body_3.wav | explosion/modules/art/slow/bombs_add/slow_attack_big-003.wav | 2 | 5.500 | 1056044 |
| bomb_far_1.wav | explosion/modules/layers_merge/bomb_body_heavy-001.wav | 1 | 6.000 | 576044 |
| bomb_far_2.wav | explosion/modules/layers_merge/bomb_body_heavy-002.wav | 1 | 6.000 | 576044 |
| bomb_far_3.wav | explosion/modules/layers_merge/bomb_body_heavy-003.wav | 1 | 5.118 | 491348 |
| debris_small_1.wav | tanks/impacts_objects/object_material_debris/dirt_debris_small-001.wav | 2 | 2.000 | 384044 |
| debris_small_2.wav | tanks/impacts_objects/object_material_debris/dirt_debris_small-002.wav | 2 | 2.000 | 384044 |
| debris_med_1.wav | tanks/impacts_objects/object_material_debris/dirt_debris_med-001.wav | 2 | 3.500 | 672044 |
| debris_med_2.wav | tanks/impacts_objects/object_material_debris/dirt_debris_med-002.wav | 2 | 3.500 | 672044 |
| debris_bomb_1.wav | tanks/impacts_objects/object_material_debris/bomb_debirs-001.wav | 2 | 3.477 | 667624 |
| debris_bomb_2.wav | tanks/impacts_objects/object_material_debris/bomb_debirs-002.wav | 2 | 3.479 | 667948 |
| ex_tail_1.wav | explosion/modules/tails/tails-001.wav | 2 | 3.501 | 672304 |
| ex_tail_2.wav | explosion/modules/tails/tails-002.wav | 2 | 3.583 | 688004 |
| ex_tail_3.wav | explosion/modules/tails/tails-003.wav | 2 | 3.585 | 688400 |
| ex_sub_1.wav | tanks/explosions/sub_sweetners/explosion_rumble_01.wav | 1 | 2.020 | 193958 |
| ex_sub_2.wav | tanks/explosions/sub_sweetners/explosion_rumble_02.wav | 1 | 1.742 | 167234 |
| ex_sub_3.wav | tanks/explosions/sub_sweetners/explosion_rumble_03.wav | 1 | 1.746 | 167706 |
| whiz_1.wav | tanks/impacts/shell_whiz_01.wav | 1 | 1.441 | 138410 |
| whiz_2.wav | tanks/impacts/shell_whiz_02.wav | 1 | 1.355 | 130168 |
| whiz_3.wav | tanks/impacts/shell_whiz_03.wav | 1 | 0.932 | 89542 |
| crack_bullet_1.wav | tanks/effects/projectile_passby/crack/bullet_crack-001.wav | 1 | 0.910 | 87404 |
| crack_bullet_2.wav | tanks/effects/projectile_passby/crack/bullet_crack-002.wav | 1 | 0.895 | 85918 |
| crack_bullet_3.wav | tanks/effects/projectile_passby/crack/bullet_crack-003.wav | 1 | 0.859 | 82472 |
| crack_bullet_4.wav | tanks/effects/projectile_passby/crack/bullet_crack-004.wav | 1 | 0.870 | 83564 |
| crack_small_1.wav | tanks/effects/projectile_passby/crack/shell_small_crack-001.wav | 1 | 1.200 | 115244 |
| crack_small_2.wav | tanks/effects/projectile_passby/crack/shell_small_crack-002.wav | 1 | 1.192 | 114480 |
| crack_small_3.wav | tanks/effects/projectile_passby/crack/shell_small_crack-003.wav | 1 | 1.194 | 114704 |
| crack_mid_1.wav | tanks/effects/projectile_passby/crack/shell_mid_crack-001.wav | 1 | 2.400 | 230444 |
| crack_mid_2.wav | tanks/effects/projectile_passby/crack/shell_mid_crack-002.wav | 1 | 2.400 | 230444 |
| crack_mid_3.wav | tanks/effects/projectile_passby/crack/shell_mid_crack-003.wav | 1 | 2.400 | 230444 |
| crack_big_1.wav | tanks/effects/projectile_passby/crack/shell_big_crack-001.wav | 1 | 2.500 | 240044 |
| crack_big_2.wav | tanks/effects/projectile_passby/crack/shell_big_crack-002.wav | 1 | 2.500 | 240044 |
| bypass_bullet_1.wav | tanks/effects/projectile_passby/bullet_bypass-001.wav | 1 | 0.562 | 54006 |
| bypass_bullet_2.wav | tanks/effects/projectile_passby/bullet_bypass-002.wav | 1 | 0.585 | 56172 |
| bypass_bullet_3.wav | tanks/effects/projectile_passby/bullet_bypass-003.wav | 1 | 0.577 | 55438 |
| bypass_bullet_4.wav | tanks/effects/projectile_passby/bullet_bypass-004.wav | 1 | 0.600 | 57644 |
| bypass_big_1.wav | tanks/effects/projectile_passby/projectile_big_bypass-001.wav | 1 | 2.000 | 192044 |
| bypass_big_2.wav | tanks/effects/projectile_passby/projectile_big_bypass-002.wav | 1 | 2.000 | 192044 |
| whiz_small_1.wav | tanks/effects/projectile_passby/projectile_small_bypass_whiz-001.wav | 1 | 0.926 | 88906 |
| whiz_small_2.wav | tanks/effects/projectile_passby/projectile_small_bypass_whiz-002.wav | 1 | 1.022 | 98162 |
| whiz_small_3.wav | tanks/effects/projectile_passby/projectile_small_bypass_whiz-003.wav | 1 | 0.906 | 87054 |
| whiz_med_1.wav | tanks/effects/projectile_passby/projectile_med_bypass_whiz-001.wav | 1 | 1.440 | 138284 |
| whiz_med_2.wav | tanks/effects/projectile_passby/projectile_med_bypass_whiz-002.wav | 1 | 1.334 | 128128 |
| whiz_med_3.wav | tanks/effects/projectile_passby/projectile_med_bypass_whiz-003.wav | 1 | 1.438 | 138140 |
| whiz_big_1.wav | tanks/effects/projectile_passby/projectile_big_bypass_whiz-001.wav | 1 | 1.977 | 189794 |
| whiz_big_2.wav | tanks/effects/projectile_passby/projectile_big_bypass_whiz-002.wav | 1 | 1.681 | 161400 |
| whiz_big_3.wav | tanks/effects/projectile_passby/projectile_big_bypass_whiz-003.wav | 1 | 1.800 | 172852 |
| incoming_art_1.wav | explosion/modules/layers_merge/art_shell_incoming-001.wav | 2 | 2.419 | 464484 |
| incoming_art_2.wav | explosion/modules/layers_merge/art_shell_incoming-002.wav | 2 | 2.386 | 458096 |
| incoming_art_3.wav | explosion/modules/layers_merge/art_shell_incoming-003.wav | 2 | 2.334 | 448092 |
| incoming_bomb_1.wav | explosion/modules/layers_merge/bombs_incoming-001.wav | 2 | 2.072 | 397784 |
| incoming_bomb_2.wav | explosion/modules/layers_merge/bombs_incoming-002.wav | 2 | 2.049 | 393420 |
| turret_loop_1.wav | tanks/effects/turret_move/turret_turn_loop_1.wav | 1 | 27.686 | 2657858 |
| turret_stop_1.wav | tanks/effects/turret_move/turret_klank_01.wav | 1 | 0.859 | 82466 |
| turret_stop_2.wav | tanks/effects/turret_move/turret_klank_02.wav | 1 | 0.770 | 73936 |
| turret_stop_3.wav | tanks/effects/turret_move/turret_klank_03.wav | 1 | 0.979 | 94070 |
| reload_open_1.wav | tanks/weapon/cannons/cannon_reloading/reloading_open-001.wav | 1 | 1.500 | 144044 |
| reload_open_2.wav | tanks/weapon/cannons/cannon_reloading/reloading_open-002.wav | 1 | 1.500 | 144044 |
| reload_open_3.wav | tanks/weapon/cannons/cannon_reloading/reloading_open-003.wav | 1 | 2.000 | 192044 |
| reload_open_4.wav | tanks/weapon/cannons/cannon_reloading/reloading_open-004.wav | 1 | 1.500 | 144044 |
| reload_eject_1.wav | tanks/weapon/cannons/cannon_reloading/reloading_eject_shell-001.wav | 1 | 1.630 | 156480 |
| reload_eject_2.wav | tanks/weapon/cannons/cannon_reloading/reloading_eject_shell-002.wav | 1 | 2.000 | 192044 |
| reload_eject_3.wav | tanks/weapon/cannons/cannon_reloading/reloading_eject_shell-003.wav | 1 | 2.000 | 192020 |
| reload_eject_4.wav | tanks/weapon/cannons/cannon_reloading/reloading_eject_shell-004.wav | 1 | 2.000 | 192044 |
| reload_insert_1.wav | tanks/weapon/cannons/cannon_reloading/reloading_insert_projectile-001.wav | 1 | 2.000 | 192044 |
| reload_insert_2.wav | tanks/weapon/cannons/cannon_reloading/reloading_insert_projectile-002.wav | 1 | 2.000 | 192044 |
| reload_insert_3.wav | tanks/weapon/cannons/cannon_reloading/reloading_insert_projectile-003.wav | 1 | 2.000 | 192044 |
| reload_insert_4.wav | tanks/weapon/cannons/cannon_reloading/reloading_insert_projectile-004.wav | 1 | 1.860 | 178568 |
| reload_close_1.wav | tanks/weapon/cannons/cannon_reloading/reloading_close-001.wav | 1 | 1.971 | 189230 |
| reload_close_2.wav | tanks/weapon/cannons/cannon_reloading/reloading_close-002.wav | 1 | 2.000 | 192044 |
| reload_close_3.wav | tanks/weapon/cannons/cannon_reloading/reloading_close-003.wav | 1 | 2.000 | 192044 |
| reload_close_4.wav | tanks/weapon/cannons/cannon_reloading/reloading_close-004.wav | 1 | 1.983 | 190396 |
| hit_bullet_1.wav | tanks/impacts/hits_recorded/tank_hit_bullet-001.wav | 1 | 0.843 | 80950 |
| hit_bullet_2.wav | tanks/impacts/hits_recorded/tank_hit_bullet-002.wav | 1 | 0.870 | 83532 |
| hit_bullet_3.wav | tanks/impacts/hits_recorded/tank_hit_bullet-003.wav | 1 | 0.898 | 86226 |
| hit_bullet_4.wav | tanks/impacts/hits_recorded/tank_hit_bullet-004.wav | 1 | 0.835 | 80200 |
| hit_bullet_5.wav | tanks/impacts/hits_recorded/tank_hit_bullet-005.wav | 1 | 0.888 | 85306 |
| hit_bullet_6.wav | tanks/impacts/hits_recorded/tank_hit_bullet-006.wav | 1 | 0.899 | 86376 |
| hit_small_1.wav | tanks/impacts/hits_recorded/tank_hit_small-001.wav | 1 | 1.853 | 177936 |
| hit_small_2.wav | tanks/impacts/hits_recorded/tank_hit_small-002.wav | 1 | 1.845 | 177152 |
| hit_small_3.wav | tanks/impacts/hits_recorded/tank_hit_small-003.wav | 1 | 1.946 | 186896 |
| hit_mid_1.wav | tanks/impacts/hits_recorded/tank_hit_mid-001.wav | 1 | 1.964 | 188630 |
| hit_mid_2.wav | tanks/impacts/hits_recorded/tank_hit_mid-002.wav | 1 | 1.963 | 188480 |
| hit_mid_3.wav | tanks/impacts/hits_recorded/tank_hit_mid-003.wav | 1 | 1.949 | 187138 |
| hit_big_1.wav | tanks/impacts/hits_recorded/tank_hit_big-002.wav | 1 | 2.447 | 234976 |
| hit_big_2.wav | tanks/impacts/hits_recorded/tank_hit_big-004.wav | 1 | 2.486 | 238664 |
| hit_pierce_1.wav | tanks/impacts/hits_recorded/tank_hit_pierce-001.wav | 1 | 2.000 | 192044 |
| hit_pierce_2.wav | tanks/impacts/hits_recorded/tank_hit_pierce-002.wav | 1 | 2.000 | 192044 |
| hit_pierce_3.wav | tanks/impacts/hits_recorded/tank_hit_pierce-003.wav | 1 | 2.000 | 192044 |
| dull_small_1.wav | tanks/impacts/hits_recorded/adds/hit_dull_small-001.wav | 1 | 1.197 | 114966 |
| dull_small_2.wav | tanks/impacts/hits_recorded/adds/hit_dull_small-002.wav | 1 | 1.197 | 114966 |
| dull_small_3.wav | tanks/impacts/hits_recorded/adds/hit_dull_small-003.wav | 1 | 1.197 | 114966 |
| dull_med_1.wav | tanks/impacts/hits_recorded/adds/hit_dull_med-001.wav | 1 | 1.197 | 114966 |
| dull_med_2.wav | tanks/impacts/hits_recorded/adds/hit_dull_med-002.wav | 1 | 1.197 | 114966 |
| dull_med_3.wav | tanks/impacts/hits_recorded/adds/hit_dull_med-003.wav | 1 | 1.197 | 114966 |
| hit_small_far_1.wav | tanks/impacts/hit_far/hit_small_far-001.wav | 1 | 2.000 | 192044 |
| hit_small_far_2.wav | tanks/impacts/hit_far/hit_small_far-002.wav | 1 | 2.000 | 192044 |
| hit_small_far_3.wav | tanks/impacts/hit_far/hit_small_far-003.wav | 1 | 2.000 | 192044 |
| hit_med_far_1.wav | tanks/impacts/hit_far/hit_med_far-001.wav | 1 | 1.951 | 187314 |
| hit_med_far_2.wav | tanks/impacts/hit_far/hit_med_far-002.wav | 1 | 1.931 | 185460 |
| hit_med_far_3.wav | tanks/impacts/hit_far/hit_med_far-003.wav | 1 | 1.944 | 186716 |
| hit_big_far_1.wav | tanks/impacts/hit_far/hit_big_far-001.wav | 1 | 2.409 | 231288 |
| hit_big_far_2.wav | tanks/impacts/hit_far/hit_big_far-002.wav | 1 | 2.441 | 234386 |
| hit_big_far_3.wav | tanks/impacts/hit_far/hit_big_far-003.wav | 1 | 2.414 | 231798 |
| rico_hit_1.wav | tanks/impacts/hit_rico/rico_hit-001.wav | 1 | 1.482 | 142320 |
| rico_hit_2.wav | tanks/impacts/hit_rico/rico_hit-002.wav | 1 | 1.481 | 142206 |
| rico_hit_3.wav | tanks/impacts/hit_rico/rico_hit-003.wav | 1 | 1.486 | 142726 |
| rico_whiz_1.wav | tanks/impacts/hit_rico/rico_whiz-001.wav | 1 | 2.000 | 192044 |
| rico_whiz_2.wav | tanks/impacts/hit_rico/rico_whiz-002.wav | 1 | 2.000 | 192044 |
| rico_whiz_3.wav | tanks/impacts/hit_rico/rico_whiz-003.wav | 1 | 2.000 | 192044 |
| bullet_whiz_1.wav | tanks/effects/projectile_whiz_rico/bullet_whiz-001.wav | 1 | 0.785 | 75386 |
| bullet_whiz_2.wav | tanks/effects/projectile_whiz_rico/bullet_whiz-002.wav | 1 | 0.787 | 75580 |
| bullet_whiz_3.wav | tanks/effects/projectile_whiz_rico/bullet_whiz-003.wav | 1 | 0.897 | 86176 |
| bullet_whiz_4.wav | tanks/effects/projectile_whiz_rico/bullet_whiz-004.wav | 1 | 0.737 | 70750 |

## The engines (src/sound_engine.h)

Source: War Thunder's own sound banks `sound/aircraft_engines.assets.bank` and `sound/tanks_engines.assets.bank`
(the streams the modder project's events above name as their files: each AudioFile's assetPath and length matched
to the bank's stream name and length), decoded by vgmstream; under the same terms as the project they belong to.
The source column: `<bank>/<subsong>_<stream name>`. Every file mixed to mono, resampled to 48000 Hz and 20 Hz high-passed by FFT over the whole file (a loop's end still runs into its start), loops kept
whole, each made seamless by a 100 ms equal-power cross-fade of its end into its start
(one-shots: `di_hiss_load`, `di_off`, `di_switch`, `jet_ab_start`, `pe_release`, `pe_revv`, `su_load`, `tr_stop`: their tails cut as above).
One gain for the engines: **0.668010** (their peak before gain 1.347285, after it 0.9).

Files: 65, 41404546 bytes (39.49 MiB, limit 40 MiB).

| file | source | channels | seconds | bytes |
|---|---|---|---|---|
| jet_idle_rear_1.wav | aircraft_engines.assets.bank/1050_typhoon_idle_rear.wav | 1 | 7.900 | 758444 |
| jet_max_rear_1.wav | aircraft_engines.assets.bank/1202_typhoon_max_rear.wav | 1 | 7.900 | 758444 |
| jet_idle_front_1.wav | aircraft_engines.assets.bank/0993_typhoon_idle_front.wav | 1 | 7.900 | 758444 |
| jet_max_front_1.wav | aircraft_engines.assets.bank/1031_typhoon_max_front.wav | 1 | 7.900 | 758444 |
| jet_ab_1.wav | aircraft_engines.assets.bank/1259_typhoon_afterburner.wav | 1 | 7.900 | 758444 |
| jet_ab_start_1.wav | aircraft_engines.assets.bank/1299_afterburner_start-001.wav | 1 | 1.834 | 176094 |
| jet_ab_start_2.wav | aircraft_engines.assets.bank/0800_afterburner_start-002.wav | 1 | 2.061 | 197896 |
| jet_ab_start_3.wav | aircraft_engines.assets.bank/0166_afterburner_start-003.wav | 1 | 1.872 | 179714 |
| jet_turb_02_1.wav | aircraft_engines.assets.bank/1207_turbine_04_N1_12800_02.wav | 1 | 9.500 | 912044 |
| jet_turb_04_1.wav | aircraft_engines.assets.bank/0408_turbine_04_N1_12800_04.wav | 1 | 9.500 | 912044 |
| jet_turb_06_1.wav | aircraft_engines.assets.bank/0316_turbine_04_N1_12800_06.wav | 1 | 9.500 | 912044 |
| jet_turb_08_1.wav | aircraft_engines.assets.bank/1117_turbine_04_N1_12800_08.wav | 1 | 9.500 | 912044 |
| jet_turb_085_1.wav | aircraft_engines.assets.bank/0119_turbine_06_N1_8867_085.wav | 1 | 9.500 | 912044 |
| jet_far_low_1.wav | aircraft_engines.assets.bank/0354_jet_low_far.wav | 1 | 13.900 | 1334444 |
| jet_far_1.wav | aircraft_engines.assets.bank/0624_jet_far_loop-001.wav | 1 | 9.400 | 902444 |
| jet_approach_1.wav | aircraft_engines.assets.bank/0709_jet-001.wav | 1 | 7.900 | 758444 |
| gt_00_1.wav | tanks_engines.assets.bank/0390_engine_00.wav | 1 | 8.300 | 796842 |
| gt_33_1.wav | tanks_engines.assets.bank/2577_engine_33.wav | 1 | 9.500 | 912044 |
| gt_66_1.wav | tanks_engines.assets.bank/0421_engine_66.wav | 1 | 7.100 | 681642 |
| gt_99_1.wav | tanks_engines.assets.bank/0810_engine_99.wav | 1 | 8.900 | 854442 |
| gt_00_load_1.wav | tanks_engines.assets.bank/0211_engine_00_load.wav | 1 | 8.293 | 796218 |
| gt_33_load_1.wav | tanks_engines.assets.bank/2496_engine_33_load.wav | 1 | 9.498 | 911832 |
| gt_66_load_1.wav | tanks_engines.assets.bank/2795_engine_66_load.wav | 1 | 7.087 | 680414 |
| gt_99_load_1.wav | tanks_engines.assets.bank/1810_engine_99_load.wav | 1 | 8.889 | 853362 |
| gt_dist_1.wav | tanks_engines.assets.bank/0033_abrams_dist-001.wav | 1 | 7.900 | 758444 |
| di_00_1.wav | tanks_engines.assets.bank/1542_strv121_engine_00_exterior.wav | 1 | 7.700 | 739244 |
| di_33_1.wav | tanks_engines.assets.bank/1937_strv121_engine_33_exterior.wav | 1 | 7.700 | 739244 |
| di_66_1.wav | tanks_engines.assets.bank/3106_strv121_engine_66_exterior.wav | 1 | 7.700 | 739244 |
| di_33_load_1.wav | tanks_engines.assets.bank/0892_strv121_engine_33_load_exterior.wav | 1 | 7.700 | 739244 |
| di_66_load_1.wav | tanks_engines.assets.bank/1818_strv121_engine_66_load_exterior.wav | 1 | 7.700 | 739244 |
| di_99_load_1.wav | tanks_engines.assets.bank/3113_strv121_engine_99_load_exterior.wav | 1 | 7.700 | 739244 |
| di_hiss_idle_1.wav | tanks_engines.assets.bank/2641_diesel_hiss_idle.wav | 1 | 9.500 | 912044 |
| di_hiss_low_1.wav | tanks_engines.assets.bank/2022_diesel_hiss_low.wav | 1 | 9.500 | 912044 |
| di_hiss_high_1.wav | tanks_engines.assets.bank/0175_diesel_hiss_high.wav | 1 | 9.500 | 912044 |
| di_switch_1.wav | tanks_engines.assets.bank/3063_strv121_engine_loadswitch_exterior-001.wav | 1 | 3.000 | 288044 |
| di_switch_2.wav | tanks_engines.assets.bank/3130_strv121_engine_loadswitch_exterior-002.wav | 1 | 2.721 | 261218 |
| di_switch_3.wav | tanks_engines.assets.bank/2792_strv121_engine_loadswitch_exterior-003.wav | 1 | 3.122 | 299778 |
| di_hiss_load_1.wav | tanks_engines.assets.bank/0497_diesel_hiss_load-001.wav | 1 | 1.992 | 191260 |
| di_hiss_load_2.wav | tanks_engines.assets.bank/1970_diesel_hiss_load-002.wav | 1 | 2.046 | 196442 |
| di_off_1.wav | tanks_engines.assets.bank/0356_engine_turbine_off-001.wav | 1 | 4.200 | 403244 |
| di_off_2.wav | tanks_engines.assets.bank/1561_engine_turbine_off-002.wav | 1 | 4.200 | 403244 |
| pe_00_1.wav | tanks_engines.assets.bank/2026_engine_00.wav | 1 | 9.500 | 912044 |
| pe_33_1.wav | tanks_engines.assets.bank/0416_engine_33.wav | 1 | 6.500 | 624044 |
| pe_66_1.wav | tanks_engines.assets.bank/1325_engine_66.wav | 1 | 7.700 | 739244 |
| pe_99_1.wav | tanks_engines.assets.bank/1589_engine_99.wav | 1 | 7.100 | 681644 |
| pe_33_load_1.wav | tanks_engines.assets.bank/1566_engine_33_load.wav | 1 | 9.500 | 912044 |
| pe_66_load_1.wav | tanks_engines.assets.bank/1136_engine_66_load.wav | 1 | 9.500 | 912044 |
| pe_99_load_1.wav | tanks_engines.assets.bank/3088_engine_99_load.wav | 1 | 9.500 | 912044 |
| pe_revv_1.wav | tanks_engines.assets.bank/1428_engine_revv-001.wav | 1 | 3.917 | 376114 |
| pe_revv_2.wav | tanks_engines.assets.bank/3038_engine_revv-002.wav | 1 | 4.126 | 396102 |
| pe_revv_3.wav | tanks_engines.assets.bank/2617_engine_revv-003.wav | 1 | 5.400 | 518442 |
| pe_release_1.wav | tanks_engines.assets.bank/0042_engine_release-001.wav | 1 | 1.634 | 156910 |
| pe_release_2.wav | tanks_engines.assets.bank/1628_engine_release-002.wav | 1 | 1.320 | 126760 |
| pe_release_3.wav | tanks_engines.assets.bank/2635_engine_release-003.wav | 1 | 1.500 | 144040 |
| tr_33_1.wav | tanks_engines.assets.bank/0266_tracks_33.wav | 1 | 7.900 | 758444 |
| tr_66_1.wav | tanks_engines.assets.bank/2185_tracks_66.wav | 1 | 7.400 | 710444 |
| tr_99_1.wav | tanks_engines.assets.bank/2729_tracks_99.wav | 1 | 7.900 | 758444 |
| tr_stop_1.wav | tanks_engines.assets.bank/2650_tracks_stop-001.wav | 1 | 2.000 | 192044 |
| tr_stop_2.wav | tanks_engines.assets.bank/0694_tracks_stop-002.wav | 1 | 2.000 | 192044 |
| tr_stop_3.wav | tanks_engines.assets.bank/0741_tracks_stop-003.wav | 1 | 2.000 | 192044 |
| su_33_1.wav | tanks_engines.assets.bank/1546_suspension_33.wav | 1 | 7.900 | 758444 |
| su_66_1.wav | tanks_engines.assets.bank/1152_suspension_66.wav | 1 | 7.900 | 758444 |
| su_99_1.wav | tanks_engines.assets.bank/1791_suspension_99.wav | 1 | 7.932 | 761554 |
| su_load_1.wav | tanks_engines.assets.bank/2250_suspension_load-001.wav | 1 | 2.851 | 273766 |
| su_load_2.wav | tanks_engines.assets.bank/1897_suspension_load-002.wav | 1 | 2.873 | 275856 |

## The jets' air (src/jet_air.h)

Source: War Thunder's own `sound/aircraft_effects.assets.bank` (the streams of the events jet_plane_passby, wind
and vibration_overload), decoded by vgmstream and made as the engines are: mono, the same resampling and high-pass,
loops (`jet_overload`, `jet_wind_high`, `jet_wind_low`) made seamless, the engines' gain (their levels are
their events', against the engines'); peak after it 0.754098.

Files: 14, 7788282 bytes (7.43 MiB, limit 10 MiB).

| file | source | channels | seconds | bytes |
|---|---|---|---|---|
| jet_passby_close_1.wav | aircraft_effects.assets.bank/0013_jet_passby_close-001.wav | 1 | 2.400 | 230444 |
| jet_passby_close_2.wav | aircraft_effects.assets.bank/0584_jet_passby_close-002.wav | 1 | 2.400 | 230444 |
| jet_passby_close_3.wav | aircraft_effects.assets.bank/0181_jet_passby_close-003.wav | 1 | 2.400 | 230444 |
| jet_passby_close_4.wav | aircraft_effects.assets.bank/0056_jet_passby_close-004.wav | 1 | 2.400 | 230444 |
| jet_passby_med_1.wav | aircraft_effects.assets.bank/0200_jet_passby_med-002.wav | 1 | 3.600 | 345644 |
| jet_passby_med_2.wav | aircraft_effects.assets.bank/0277_jet_passby_med-003.wav | 1 | 3.518 | 337746 |
| jet_passby_med_3.wav | aircraft_effects.assets.bank/0376_jet_passby_med-005.wav | 1 | 3.466 | 332796 |
| jet_passby_far_1.wav | aircraft_effects.assets.bank/0486_jet_passby_far-001.wav | 1 | 12.000 | 1152044 |
| jet_passby_far_2.wav | aircraft_effects.assets.bank/0572_jet_passby_far-003.wav | 1 | 10.056 | 965430 |
| jet_passby_fast_1.wav | aircraft_effects.assets.bank/0111_passby_jet_close-003.wav | 1 | 7.082 | 679942 |
| jet_passby_fast_2.wav | aircraft_effects.assets.bank/0058_passby_jet_close-001.wav | 1 | 5.512 | 529234 |
| jet_wind_low_1.wav | aircraft_effects.assets.bank/0470_wind_low.wav | 1 | 14.527 | 1394680 |
| jet_wind_high_1.wav | aircraft_effects.assets.bank/0542_wind_hi_4.wav | 1 | 6.731 | 646242 |
| jet_overload_1.wav | aircraft_effects.assets.bank/0567_plane_overload_loop.wav | 1 | 5.028 | 482748 |
