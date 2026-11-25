# War Thunder gun, explosion and passby sounds (assets/sound/wt)

Source: https://github.com/GaijinEntertainment/fmod_studio_warthunder_for_modders , commit `07fd12efffbf20b2053d630ed5cf7ca3ddfa54a6`.
The upstream Readme says the project "allows you to create sound modifications" and that using the content means
agreeing to the War Thunder contribution agreement (https://live.warthunder.com/contribution_agreement/?lang=en).
The repository owner allowed committing the processed clips; the terms are those of the upstream repository.
The upstream assets are already vorbis-compressed (ogg renamed to .wav); they are decoded once here and written as
16-bit PCM.

Made by `tools/wt_sound.py` (clip names and variant counts: src/wt_clips.h CREW_WT_CLIPS). Per file: channels kept
(`art_attack_far`, `bomb_far`, `bypass_big`, `bypass_bullet`, `crack_big`, `crack_bullet`, `crack_mid`, `crack_small`, `ex_attack_far`, `ex_sub`, `reload_close`, `reload_eject`, `reload_insert`, `reload_open`, `sub`, `turret_loop`, `turret_stop`, `whiz`, `whiz_big`, `whiz_med`, `whiz_small`, `xs_far`: mixed to mono), resampled to 48000 Hz, 20 Hz high-pass, no
per-file levelling or normalising, start kept, tail under -60 dBFS cut with a 20 ms fade-out
(loops kept whole: `turret_loop`);
cut to a length (300 ms fade-out): close_150 5.0 s, tail_heavy 7.0 s, crack_big 2.5 s, bypass_big 2.0 s.
One gain for the whole set: **0.680167** (set peak before gain 1.440824; scaled only when over 0.98).

Installed to `Mods/Plugins/EDF6VehicleCrew_wt/<name>_<i>.wav` by the installer.

Files: 192, total size 68436586 bytes (65.27 MiB).

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
