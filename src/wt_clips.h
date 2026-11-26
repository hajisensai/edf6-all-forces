#pragma once
// The War Thunder recordings the ground vehicles' guns, the explosions and the rounds going by are layered from, and the
// turret's and the loader's (jetaudio.cpp kWtFor)
// (docs/feature-wt-sound.md): each a group of variants, installed as Mods/Plugins/<dll name>_wt/<name>_<1..n>.wav (16-bit PCM 48 kHz, mono or stereo), made from
// GaijinEntertainment/fmod_studio_warthunder_for_modders by tools/wt_sound.py (its table says which file each one is;
// assets/sound/wt/SOURCE.md lists them). tools/wt_sound.py and tests/wt_sound_test.py read this list: the same names,
// the same counts. A group with a variant missing is not loaded (logged once): its layer stays silent.
//
// X(name, variants)
#define CREW_WT_CLIPS(X) \
    X(shot_mid,3) X(shot_big,3) X(shot_heavy,3) X(middle_mid,2) X(middle_big,2) X(bass,2) X(body_big,2) X(metal,2) \
    X(mech_howitzer,2) X(close_75,3) X(close_122,3) X(close_128,3) X(close_150,3) X(far_75,3) X(far_122,3) X(far_128,3) \
    X(far_150,3) X(bump_med,3) X(bump_big,3) X(sub,3) X(tail_mid,2) X(tail_big,2) X(tail_heavy,2) \
    X(m2_close,6) X(m2_far,6) X(m2_last_close,2) X(m2_last_far,2) \
    X(mg42_close,6) X(mg42_far,6) X(mg42_last_close,2) X(mg42_last_far,2) \
    X(xs_close,3) X(xs_far,3) X(ex_sweet,3) X(ex_attack,3) X(ex_attack_far,3) X(ex_body,3) X(art_attack,3) X(art_attack_far,3) \
    X(art_body,3) X(bomb_body,3) X(bomb_far,3) X(debris_small,2) X(debris_med,2) X(debris_bomb,2) X(ex_tail,3) X(ex_sub,3) \
    X(whiz,3) X(crack_bullet,4) X(crack_small,3) X(crack_mid,3) X(crack_big,2) X(bypass_bullet,4) X(bypass_big,2) \
    X(whiz_small,3) X(whiz_med,3) X(whiz_big,3) X(incoming_art,3) X(incoming_bomb,2) \
    X(turret_loop,1) X(turret_stop,3) X(reload_open,4) X(reload_eject,4) X(reload_insert,4) X(reload_close,4) \
    X(jet_idle_rear,1) X(jet_max_rear,1) X(jet_idle_front,1) X(jet_max_front,1) X(jet_ab,1) X(jet_ab_start,3) \
    X(jet_turb_02,1) X(jet_turb_04,1) X(jet_turb_06,1) X(jet_turb_08,1) X(jet_turb_085,1) X(jet_far_low,1) \
    X(jet_far,1) X(jet_approach,1) X(gt_00,1) X(gt_33,1) X(gt_66,1) X(gt_99,1) X(gt_00_load,1) X(gt_33_load,1) \
    X(gt_66_load,1) X(gt_99_load,1) X(gt_dist,1) X(di_00,1) X(di_33,1) X(di_66,1) X(di_33_load,1) X(di_66_load,1) \
    X(di_99_load,1) X(di_hiss_idle,1) X(di_hiss_low,1) X(di_hiss_high,1) X(di_switch,3) X(di_hiss_load,2) X(di_off,2) \
    X(pe_00,1) X(pe_33,1) X(pe_66,1) X(pe_99,1) X(pe_33_load,1) X(pe_66_load,1) X(pe_99_load,1) X(pe_revv,3) \
    X(pe_release,3) X(tr_33,1) X(tr_66,1) X(tr_99,1) X(tr_stop,3) X(su_33,1) X(su_66,1) X(su_99,1) X(su_load,2)

namespace crew::wt {
#define CREW_WT_ENUM(name,n) k_##name,
enum Clip : int { CREW_WT_CLIPS(CREW_WT_ENUM) kCount };
#undef CREW_WT_ENUM
#define CREW_WT_NAME(name,n) L"" #name,
inline constexpr const wchar_t* kName[kCount]={CREW_WT_CLIPS(CREW_WT_NAME)};
#undef CREW_WT_NAME
#define CREW_WT_VARIANTS(name,n) n,
inline constexpr int kVariants[kCount]={CREW_WT_CLIPS(CREW_WT_VARIANTS)};
#undef CREW_WT_VARIANTS
constexpr int kMostVariants=6;
}  // namespace crew::wt
