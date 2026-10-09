// The HUD's texts (src/hudtext.h): HUDTEXT(key, English, Simplified Chinese, Traditional Chinese, Japanese).
// A text is a printf format: each language takes the English one's arguments, in its order (tools/hudtext_check.cpp).
// The Chinese and Japanese follow the game's own words where it has them (its text tables, Root.cpk
// ETC/TEXTTABLE_STEAM.*.TXT_SGO): 飞弹 / 飛彈 / ミサイル, 机炮 / 機砲 / 機関砲, 加农炮 / 加農砲 / キャノン, 装填 / 裝填 / リロード,
// 载具 / 載具 / ビークル, 坐席 / 座席, 驾驶席, 准星, 加速, 减速, 替换武器, 鼠标左键 / 滑鼠左鍵 / マウス左ボタン; the plugin's own
// units by the names README.md gives them (炮舰机, 潜水母舰, 钻头战车, 深渊爬行者, 人偶无人机 ...).

// --- Over a friendly NPC vehicle (hud.cpp Readouts) ---
HUDTEXT(readoutGuns,L"  G %d",L"  机炮 %d",L"  機砲 %d",L"  機関砲 %d")
HUDTEXT(readoutMissiles,L"  M %d",L"  飞弹 %d",L"  飛彈 %d",L"  ミサイル %d")
HUDTEXT(readoutDrones,L"  D %d",L"  无人机 %d",L"  無人機 %d",L"  ドローン %d")
HUDTEXT(readoutFuel,L"  F %d:%02d",L"  燃料 %d:%02d",L"  燃料 %d:%02d",L"  燃料 %d:%02d")
HUDTEXT(readoutRtb,L"  RTB",L"  返航",L"  返航",L"  帰投")

// --- The submarine carrier's panel and badge ---
HUDTEXT(carrierTitleN,L"SUBMARINE CARRIER %d",L"潜水母舰 %d",L"潛水母艦 %d",L"潜水母艦 %d")
HUDTEXT(carrierTitle,L"SUBMARINE CARRIER",L"潜水母舰",L"潛水母艦",L"潜水母艦")
HUDTEXT(carrierHull,L"HULL %d%%   %.0f / %.0f",L"舰体 %d%%   %.0f / %.0f",L"艦體 %d%%   %.0f / %.0f",L"船体 %d%%   %.0f / %.0f")
HUDTEXT(carrierPartDown,L"%ls   DOWN  repair %d:%02d",L"%ls   损毁  修复 %d:%02d",L"%ls   損毀  修復 %d:%02d",L"%ls   損壊  修理 %d:%02d")
HUDTEXT(carrierBadge,L"SUBMARINE CARRIER  %d%%",L"潜水母舰  %d%%",L"潛水母艦  %d%%",L"潜水母艦  %d%%")
HUDTEXT(carrierPartDownShort,L"%ls  DOWN %d:%02d",L"%ls  损毁 %d:%02d",L"%ls  損毀 %d:%02d",L"%ls  損壊 %d:%02d")
HUDTEXT(partTurretA,L"turretA",L"炮塔A",L"砲塔A",L"砲塔A")
HUDTEXT(partTurretB,L"turretB",L"炮塔B",L"砲塔B",L"砲塔B")
HUDTEXT(partMissiles,L"missiles",L"飞弹舱",L"飛彈艙",L"ミサイル区画")
HUDTEXT(partDroneBay,L"dronebay",L"无人机舱",L"無人機艙",L"ドローン格納庫")

// --- Aim marks and cues ---
HUDTEXT(launcherLine,L"%d m   %.1f s   ELEV %d",L"%d m   %.1f s   仰角 %d",L"%d m   %.1f s   仰角 %d",L"%d m   %.1f s   仰角 %d")
HUDTEXT(padButton,L"button 0x%X",L"手柄键 0x%X",L"手把鍵 0x%X",L"ボタン 0x%X")
HUDTEXT(highCamOn,L"HIGH CAM ON [%ls]",L"俯瞰视角 开 [%ls]",L"俯瞰視角 開 [%ls]",L"俯瞰カメラ オン [%ls]")
HUDTEXT(highCamOff,L"HIGH CAM [%ls]",L"俯瞰视角 [%ls]",L"俯瞰視角 [%ls]",L"俯瞰カメラ [%ls]")
HUDTEXT(sightRange,L"LRF %d m",L"测距 %d m",L"測距 %d m",L"測距 %d m")
HUDTEXT(sightNoRange,L"LRF ----",L"测距 ----",L"測距 ----",L"測距 ----")
HUDTEXT(gunnerShells,L"SHELLS",L"炮弹",L"砲彈",L"砲弾")
HUDTEXT(gunnerCannon,L"CANNON",L"加农炮",L"加農砲",L"キャノン")
HUDTEXT(gunnerGatling,L"GATLING",L"加特林",L"加特林",L"ガトリング")
HUDTEXT(gunnerNoGround,L"%ls   NO GROUND IN SIGHT",L"%ls   视野内没有地面",L"%ls   視野內沒有地面",L"%ls   地面が見えない")
HUDTEXT(gunnerOutOfRange,L"%ls   %d m   OUT OF RANGE",L"%ls   %d m   超出射程",L"%ls   %d m   超出射程",L"%ls   %d m   射程外")
HUDTEXT(gunnerReady,L"%ls   %d m   READY",L"%ls   %d m   就绪",L"%ls   %d m   就緒",L"%ls   %d m   発射可")
HUDTEXT(outOfRange,L"OUT OF RANGE",L"超出射程",L"超出射程",L"射程外")
HUDTEXT(fly,L"FLY",L"飞行",L"飛行",L"飛行")
HUDTEXT(missileRange,L"MSL %d m",L"飞弹 %d m",L"飛彈 %d m",L"ミサイル %d m")

// --- The seats line (seatswitch.cpp) ---
HUDTEXT(seats,L"SEATS",L"坐席",L"座席",L"座席")
HUDTEXT(holderYou,L"YOU",L"你",L"你",L"自分")
HUDTEXT(holderNpc,L"NPC",L"NPC",L"NPC",L"NPC")
HUDTEXT(holderTaken,L"TAKEN",L"有人",L"有人",L"使用中")
HUDTEXT(seatPilot,L"PILOT",L"驾驶席",L"駕駛席",L"操縦席")
HUDTEXT(seatDriver,L"DRIVER",L"驾驶席",L"駕駛席",L"運転席")
HUDTEXT(seatGun,L"GUN",L"炮手席",L"砲手席",L"砲手席")
HUDTEXT(seatOther,L"SEAT",L"坐席",L"座席",L"座席")
HUDTEXT(seatOnlineLocked,L"   (online: SeatSwitchOnline=0)",L"   （联机：SeatSwitchOnline=0）",L"   （連線：SeatSwitchOnline=0）",L"   （オンライン：SeatSwitchOnline=0）")
HUDTEXT(seatNextKey,L"   [%ls] next",L"   [%ls] 下一个",L"   [%ls] 下一個",L"   [%ls] 次へ")
HUDTEXT(seatPick,L"   [1-%d] pick",L"   [1-%d] 选择",L"   [1-%d] 選擇",L"   [1-%d] 選択")
HUDTEXT(seatNoFree,L"   NO FREE SEAT",L"   没有空位",L"   沒有空位",L"   空席なし")
HUDTEXT(seatTaken,L"   SEAT %d TAKEN",L"   坐席 %d 有人",L"   座席 %d 有人",L"   座席 %d 使用中")

// --- The jets' cockpit (the strip, the old panel) and the fuel ---
HUDTEXT(storesGun,L"GUN %d  ",L"机炮 %d  ",L"機砲 %d  ",L"機関砲 %d  ")
HUDTEXT(storesFlare,L"FLARE %d",L"热焰弹 %d",L"熱焰彈 %d",L"フレア %d")
HUDTEXT(fuelTime,L"FUEL %d%% %d:%02d",L"燃料 %d%% %d:%02d",L"燃料 %d%% %d:%02d",L"燃料 %d%% %d:%02d")
HUDTEXT(fuelShare,L"FUEL %d%%",L"燃料 %d%%",L"燃料 %d%%",L"燃料 %d%%")
HUDTEXT(rotateNow,L"ROTATE: PULL UP (W / SPACE)",L"抬头：拉起（W / 空格）",L"抬頭：拉起（W / 空白鍵）",L"機首上げ：引き起こし（W / スペース）")
HUDTEXT(rotateAt,L"ROTATE AT %d km/h",L"抬头速度 %d km/h",L"抬頭速度 %d km/h",L"機首上げ %d km/h")
HUDTEXT(pullUpTerrain,L"PULL UP! TERRAIN",L"拉起！地形",L"拉起！地形",L"プルアップ！地表")
HUDTEXT(missileBang,L"MISSILE!",L"飞弹！",L"飛彈！",L"ミサイル！")
HUDTEXT(locked,L"LOCKED",L"被锁定",L"被鎖定",L"被ロック")
HUDTEXT(lowFuel,L"LOW FUEL",L"燃料不足",L"燃料不足",L"燃料低下")
HUDTEXT(jetInfo,L"SPD %d km/h    ALT%ls %d m    %ls %d m/s    HP %d%%",L"速度 %d km/h    高度%ls %d m    %ls %d m/s    耐久 %d%%",
        L"速度 %d km/h    高度%ls %d m    %ls %d m/s    耐久 %d%%",L"速度 %d km/h    高度%ls %d m    %ls %d m/s    耐久 %d%%")
HUDTEXT(climbUp,L"UP",L"上升",L"上升",L"上昇")
HUDTEXT(climbDown,L"DOWN",L"下降",L"下降",L"下降")
HUDTEXT(jetThrottle,L"THROTTLE %d%%%ls%ls    G %.1f%ls%ls%ls",L"油门 %d%%%ls%ls    G %.1f%ls%ls%ls",L"油門 %d%%%ls%ls    G %.1f%ls%ls%ls",
        L"スロットル %d%%%ls%ls    G %.1f%ls%ls%ls")
HUDTEXT(stall,L"STALL",L"失速",L"失速",L"失速")
HUDTEXT(jetKeysMouse,L"MOUSE: aim (the square)    W / SPACE: pull up    S: push down    A / D: roll",
        L"鼠标：瞄准（方框）    W / 空格：拉起    S：压低    A / D：滚转",L"滑鼠：瞄準（方框）    W / 空白鍵：拉起    S：壓低    A / D：滾轉",
        L"マウス：照準（四角）    W / スペース：機首上げ    S：機首下げ    A / D：ロール")
HUDTEXT(jetKeysHand,L"W / SPACE: pull up    S: push down    A / D: roll    (let go: wings level)",
        L"W / 空格：拉起    S：压低    A / D：滚转    （松开：改平）",L"W / 空白鍵：拉起    S：壓低    A / D：滾轉    （放開：改平）",
        L"W / スペース：機首上げ    S：機首下げ    A / D：ロール    （離すと水平）")
HUDTEXT(jetKeysMouseHand,L"MOUSE: aim    W / SPACE / S / A / D: fly by hand (the mouse takes over once it moves)",
        L"鼠标：瞄准    W / 空格 / S / A / D：手动飞行（鼠标一动即接管）",L"滑鼠：瞄準    W / 空白鍵 / S / A / D：手動飛行（滑鼠一動即接管）",
        L"マウス：照準    W / スペース / S / A / D：手動操縦（マウスを動かすと戻る）")
HUDTEXT(jetKeysAir,L"%ls: boost    %ls: brake    %ls: switch weapon    %ls: next target    %ls: flares",
        L"%ls：加速    %ls：减速    %ls：替换武器    %ls：下一个目标    %ls：热焰弹",L"%ls：加速    %ls：減速    %ls：替換武器    %ls：下一個目標    %ls：熱焰彈",
        L"%ls：ブースト    %ls：ブレーキ    %ls：武器切替    %ls：次の目標    %ls：フレア")
HUDTEXT(jetKeysGround,L"%ls: throttle up    %ls: throttle down    A / D, MOUSE: steer",L"%ls：加油门    %ls：减油门    A / D、鼠标：转向",
        L"%ls：加油門    %ls：減油門    A / D、滑鼠：轉向",L"%ls：スロットル上げ    %ls：スロットル下げ    A / D・マウス：旋回")
HUDTEXT(jetKeysTakeoff,L"W / SPACE: pull up to take off (from %d km/h)",L"W / 空格：拉起起飞（%d km/h 起）",L"W / 空白鍵：拉起起飛（%d km/h 起）",
        L"W / スペース：引き起こして離陸（%d km/h から）")
HUDTEXT(jetPadAir,L"BOOST: forward / ascend    BRAKE: back    ROLL: left stick sideways",L"加速：前进 / 上升    减速：后退    滚转：左摇杆左右",
        L"加速：前進 / 上升    減速：後退    滾轉：左搖桿左右",L"ブースト：前進 / 上昇    ブレーキ：後退    ロール：左スティック左右")
HUDTEXT(jetPadAir2,L"PITCH, TURN: right stick    %ls: stores    %ls: target    keyboard %ls: flares",L"俯仰、转向：右摇杆    %ls：挂载    %ls：目标    键盘 %ls：热诱弹",
        L"俯仰、轉向：右搖桿    %ls：掛載    %ls：目標    鍵盤 %ls：熱誘彈",L"ピッチ・旋回：右スティック    %ls：兵装    %ls：目標    キーボード %ls：フレア")
HUDTEXT(jetPadGround,L"THROTTLE: forward / ascend = up, back = down    TURN: sticks",L"油门：前进 / 上升 = 加，后退 = 减    转向：摇杆",
        L"油門：前進 / 上升 = 加，後退 = 減    轉向：搖桿",L"スロットル：前進 / 上昇 = 上げ、後退 = 下げ    旋回：スティック")
HUDTEXT(jetPadTakeoff,L"TAKE OFF: pull the right stick back (from %d km/h)",L"起飞：右摇杆向后拉（%d km/h 起）",L"起飛：右搖桿向後拉（%d km/h 起）",
        L"離陸：右スティックを引く（%d km/h から）")
HUDTEXT(strip,L"THR %d%%%ls%ls    %ls",L"油门 %d%%%ls%ls    %ls",L"油門 %d%%%ls%ls    %ls",L"スロットル %d%%%ls%ls    %ls")

// --- The flight instruments ---
HUDTEXT(speedLabel,L"KM/H",L"速度 km/h",L"速度 km/h",L"速度 km/h")
HUDTEXT(altLabel,L"ALT M",L"高度 m",L"高度 m",L"高度 m")
HUDTEXT(altLabelNoGround,L"ALT* M",L"高度* m",L"高度* m",L"高度* m")
HUDTEXT(climbRate,L"VS %+d",L"升降 %+d",L"升降 %+d",L"昇降 %+d")
HUDTEXT(launch,L"LAUNCH",L"发射",L"發射",L"発射")
HUDTEXT(pullUp,L"PULL UP",L"拉起",L"拉起",L"プルアップ")
HUDTEXT(lift,L"LIFT",L"升力",L"升力",L"揚力")
HUDTEXT(compassN,L"N",L"北",L"北",L"北")
HUDTEXT(compassS,L"S",L"南",L"南",L"南")
HUDTEXT(compassE,L"E",L"东",L"東",L"東")
HUDTEXT(compassW,L"W",L"西",L"西",L"西")

// --- The annunciator (warn.h) ---
HUDTEXT(warnPullUp,L"PULL UP",L"拉起",L"拉起",L"プルアップ")
HUDTEXT(warnMissile,L"MISSILE",L"飞弹",L"飛彈",L"ミサイル")
HUDTEXT(warnStall,L"STALL",L"失速",L"失速",L"失速")
HUDTEXT(warnGear,L"GEAR",L"起落架",L"起落架",L"ギア")
HUDTEXT(warnTerrain,L"TERRAIN",L"地形",L"地形",L"地表")
HUDTEXT(warnSinkRate,L"SINK RATE",L"下沉率",L"下沉率",L"降下率")
HUDTEXT(warnLock,L"LOCK",L"锁定",L"鎖定",L"ロック")
HUDTEXT(warnGearSpeed,L"GEAR SPEED",L"起落架超速",L"起落架超速",L"ギア速度超過")
HUDTEXT(warnWeightOnWheels,L"WEIGHT ON WHEELS",L"轮上有载",L"輪上有載",L"接地中")
HUDTEXT(warnLowFuel,L"LOW FUEL",L"燃料不足",L"燃料不足",L"燃料低下")
HUDTEXT(masterWarn,L"MASTER WARN",L"主警告",L"主警告",L"マスター警告")
HUDTEXT(masterCaution,L"MASTER CAUTION",L"主注意",L"主注意",L"マスター注意")
HUDTEXT(missileLaunch,L"MSL LAUNCH",L"飞弹发射",L"飛彈發射",L"ミサイル発射")

// --- The helicopters (HeliStrip, HeliHud) and the gear ---
HUDTEXT(heliSpeedLabel,L"GROUND KM/H",L"水平速 km/h",L"水平速 km/h",L"水平速度 km/h")
HUDTEXT(heliHeightLabel,L"AGL M",L"离地 m",L"離地 m",L"対地高度 m")
HUDTEXT(heliThreatScope,L"THREATS",L"威胁方向",L"威脅方向",L"脅威方向")
HUDTEXT(setHover,L"FWD SET 0",L"前速目标 0",L"前速目標 0",L"前進設定 0")
HUDTEXT(setSpeed,L"FWD SET %d",L"前速目标 %d",L"前速目標 %d",L"前進設定 %d")
HUDTEXT(heliKeysLanded,L"HOLD SPACE: spin the rotor up and lift off    MOUSE: aim the square",L"按住空格：旋翼加速并升空    鼠标：瞄准方框",
        L"按住空白鍵：旋翼加速並升空    滑鼠：瞄準方框",L"スペース長押し：ローター加速で離陸    マウス：四角で照準")
HUDTEXT(heliKeysInstructorLanded,L"HOLD W / SPACE: spin the rotor up and lift off    MOUSE: aim the square",
        L"按住 W / 空格：旋翼加速并升空    鼠标：瞄准方框",L"按住 W / 空白鍵：旋翼加速並升空    滑鼠：瞄準方框",
        L"W / スペース長押し：ローター加速で離陸    マウス：四角で照準")
HUDTEXT(heliKeysInstructor,L"MOUSE: heading / nose (down: forward)    W / S: up / down    A / D: slide    SPACE: up    %ls: down",
        L"鼠标：航向 / 机头（压低 = 前进）    W / S：上升 / 下降    A / D：横移    空格：上升    %ls：下降",
        L"滑鼠：航向 / 機首（壓低 = 前進）    W / S：上升 / 下降    A / D：橫移    空白鍵：上升    %ls：下降",
        L"マウス：方位 / 機首（下げると前進）    W / S：上昇 / 下降    A / D：横移動    スペース：上昇    %ls：下降")
HUDTEXT(heliKeysAir,L"MOUSE: turn / pitch    W / S: forward speed    A / D: slide    SPACE: up    %ls: down",
        L"鼠标：转向 / 俯仰    W / S：前速    A / D：横移    空格：上升    %ls：下降",
        L"滑鼠：轉向 / 俯仰    W / S：前速    A / D：橫移    空白鍵：上升    %ls：下降",
        L"マウス：旋回 / 機首上下    W / S：前進速度    A / D：横移動    スペース：上昇    %ls：下降")
HUDTEXT(liftOk,L"LIFT OK: TAKE OFF (ascend)",L"升力足够：起飞（上升）",L"升力足夠：起飛（上升）",L"揚力十分：離陸（上昇）")
HUDTEXT(rotorShare,L"ROTOR %d%% of lift-off",L"旋翼 %d%%（离地所需）",L"旋翼 %d%%（離地所需）",L"ローター %d%%（離陸に必要な回転）")
HUDTEXT(rotorLiftOk,L"ROTOR %d%%    LIFT OK: TAKE OFF (ascend)",L"旋翼 %d%%    升力足够：起飞（上升）",L"旋翼 %d%%    升力足夠：起飛（上升）",
        L"ローター %d%%    揚力十分：離陸（上昇）")
HUDTEXT(speedSetHover,L"FORWARD HOLD 0 km/h",L"前速保持 0 km/h",L"前速保持 0 km/h",L"前進速度維持 0 km/h")
HUDTEXT(speedSet,L"FORWARD HOLD %d km/h",L"前速保持 %d km/h",L"前速保持 %d km/h",L"前進速度維持 %d km/h")
HUDTEXT(altHold,L"%lsALTITUDE HOLD",L"%ls垂直：定高",L"%ls垂直：定高",L"%ls垂直：高度維持")
HUDTEXT(gearState,L"GEAR %ls  [%ls]",L"起落架 %ls  [%ls]",L"起落架 %ls  [%ls]",L"ギア %ls  [%ls]")
HUDTEXT(gearDown,L"DOWN",L"放下",L"放下",L"ダウン")
HUDTEXT(gearUp,L"UP",L"收起",L"收起",L"アップ")
HUDTEXT(gearTransit,L"TRANSIT",L"转换中",L"轉換中",L"作動中")
HUDTEXT(gearBang,L"GEAR!",L"起落架！",L"起落架！",L"ギア！")

// --- The drill tank, the turret aim (EDF6AutoTurret), the Proteus, the EMC ---
HUDTEXT(drillLine,L"DRILL %d RPM    HEAT %d%%%ls",L"钻头 %d RPM    热量 %d%%%ls",L"鑽頭 %d RPM    熱量 %d%%%ls",L"ドリル %d RPM    熱 %d%%%ls")
HUDTEXT(drillLineShort,L"DRILL %d RPM  HEAT %d%%%ls",L"钻头 %d RPM  热量 %d%%%ls",L"鑽頭 %d RPM  熱量 %d%%%ls",L"ドリル %d RPM  熱 %d%%%ls")
HUDTEXT(overheat,L"OVERHEAT",L"过热",L"過熱",L"オーバーヒート")
HUDTEXT(drilling,L"DRILLING",L"钻削中",L"鑽削中",L"掘削中")
HUDTEXT(drillLaunched,L"LAUNCHED",L"发射中",L"發射中",L"射出中")
HUDTEXT(drillReturning,L"RETURNING",L"回收中",L"回收中",L"回収中")
HUDTEXT(drillLaunchHint,L"[%ls] LAUNCH",L"[%ls] 发射",L"[%ls] 發射",L"[%ls] 射出")
HUDTEXT(gunnersAuto,L"GUNNERS: AUTO-AIM",L"炮手：自动瞄准",L"砲手：自動瞄準",L"砲手：自動照準")
HUDTEXT(autoAimOffCircle,L"AUTO-AIM OFF  (LEAD CIRCLE)",L"自动瞄准 关  （预瞄圈）",L"自動瞄準 關  （預瞄圈）",L"自動照準 オフ  （偏差円）")
HUDTEXT(autoAimOn,L"AUTO-AIM ON",L"自动瞄准 开",L"自動瞄準 開",L"自動照準 オン")
HUDTEXT(autoAimToggle,L"[%ls] auto-aim %ls",L"[%ls] 自动瞄准 %ls",L"[%ls] 自動瞄準 %ls",L"[%ls] 自動照準 %ls")
HUDTEXT(wordOn,L"on",L"开",L"開",L"オン")
HUDTEXT(wordOff,L"off",L"关",L"關",L"オフ")
HUDTEXT(aimModeKeyUnset,L"(no AimModeKey set)",L"（未设置 AimModeKey）",L"（未設定 AimModeKey）",L"（AimModeKey 未設定）")
HUDTEXT(aimModeButtonUnset,L"(pad: set AimModeButton in EDF6AutoTurret.ini)",L"（手柄：在 EDF6AutoTurret.ini 设置 AimModeButton）",
        L"（手把：在 EDF6AutoTurret.ini 設定 AimModeButton）",L"（パッド：EDF6AutoTurret.ini で AimModeButton を設定）")
HUDTEXT(lockKey,L"%ls[%ls] %ls (hold: clear)",L"%ls[%ls] %ls（长按：清除）",L"%ls[%ls] %ls（長按：清除）",L"%ls[%ls] %ls（長押し：解除）")
HUDTEXT(lockWord,L"lock",L"锁定",L"鎖定",L"ロック")
HUDTEXT(nextWord,L"next",L"下一个",L"下一個",L"次")
HUDTEXT(autoAimOffBanner,L"AUTO-AIM OFF  -  LEAD CIRCLE",L"自动瞄准 关  -  预瞄圈",L"自動瞄準 關  -  預瞄圈",L"自動照準 オフ  -  偏差円")
HUDTEXT(hullMark,L"HULL",L"车体",L"車體",L"車体")
HUDTEXT(mouseLeft,L"LMB",L"左键",L"左鍵",L"左クリック")
HUDTEXT(mouseRight,L"RMB",L"右键",L"右鍵",L"右クリック")
HUDTEXT(mouseMiddle,L"MMB",L"中键",L"中鍵",L"中クリック")
HUDTEXT(mouseX1,L"MB4",L"鼠标键1",L"滑鼠鍵1",L"マウスボタン1")
HUDTEXT(mouseX2,L"MB5",L"鼠标键2",L"滑鼠鍵2",L"マウスボタン2")
HUDTEXT(proteusWalk,L"WALK",L"步行",L"步行",L"歩行")
HUDTEXT(proteusDeployKey,L"   [%ls] DEPLOY",L"   [%ls] 展开",L"   [%ls] 展開",L"   [%ls] 展開")
HUDTEXT(proteusDeploying,L"DEPLOYING %d%%",L"展开中 %d%%",L"展開中 %d%%",L"展開中 %d%%")
HUDTEXT(proteusDeployed,L"DEPLOYED",L"已展开",L"已展開",L"展開完了")
HUDTEXT(proteusStowKey,L"   [%ls] STOW",L"   [%ls] 收起",L"   [%ls] 收起",L"   [%ls] 収納")
HUDTEXT(proteusStowing,L"STOWING %d%%",L"收起中 %d%%",L"收起中 %d%%",L"収納中 %d%%")
HUDTEXT(shieldHeat,L"SHIELD %ls  %d%%  HEAT %d%%",L"护盾 %ls  %d%%  热量 %d%%",L"護盾 %ls  %d%%  熱量 %d%%",L"シールド %ls  %d%%  熱 %d%%")
HUDTEXT(frontShield,L"SHIELD %ls  %d%%",L"护盾 %ls  %d%%",L"護盾 %ls  %d%%",L"シールド %ls  %d%%")
HUDTEXT(shieldUp,L"UP",L"开启",L"開啟",L"展開")
HUDTEXT(shieldOff,L"OFF",L"关闭",L"關閉",L"オフ")
HUDTEXT(shieldBroken,L"BROKEN",L"已击碎",L"已擊碎",L"破損")
HUDTEXT(shieldOffline,L"OFFLINE",L"不可用",L"不可用",L"使用不可")
HUDTEXT(alliesFocus,L"   ALLIES FOCUS",L"   友军集火",L"   友軍集火",L"   味方集中攻撃")
HUDTEXT(field,L"FIELD %d m  ALLIES %d",L"力场 %d m  友军 %d",L"力場 %d m  友軍 %d",L"フィールド %d m  味方 %d")
HUDTEXT(launcherKey,L"MISSILES [%ls]",L"导弹 [%ls]",L"飛彈 [%ls]",L"ミサイル [%ls]")
HUDTEXT(launcherDeployFirst,L"MISSILES  DEPLOY FIRST",L"导弹  请先展开",L"飛彈  請先展開",L"ミサイル  先に展開")
HUDTEXT(armReady,L"READY",L"就绪",L"就緒",L"発射可")
HUDTEXT(armReload,L"RELOAD %d%%",L"装填 %d%%",L"裝填 %d%%",L"リロード %d%%")
HUDTEXT(armReloadWord,L"RELOAD",L"装填",L"裝填",L"リロード")
HUDTEXT(armEmpty,L"EMPTY",L"弹尽",L"彈盡",L"弾切れ")
HUDTEXT(emcBeam,L"EMC BEAM %.1fs",L"EMC 照射 %.1fs",L"EMC 照射 %.1fs",L"EMC 照射 %.1fs")
HUDTEXT(emcCharge,L"EMC CHARGE %d%%",L"EMC 充能 %d%%",L"EMC 充能 %d%%",L"EMC チャージ %d%%")
HUDTEXT(emcEmpty,L"EMC EMPTY",L"EMC 能量耗尽",L"EMC 能量耗盡",L"EMC エネルギー切れ")
HUDTEXT(emcRearm,L"EMC REARM %.1fs",L"EMC 再装填 %.1fs",L"EMC 再裝填 %.1fs",L"EMC 再装填 %.1fs")
HUDTEXT(emcReady,L"EMC READY x%d  HOLD FIRE",L"EMC 就绪 x%d  按住射击",L"EMC 就緒 x%d  按住射擊",L"EMC 準備完了 x%d  射撃長押し")

// --- The stock vehicles' block (StockBlock) ---
HUDTEXT(hullCritical,L"HULL CRITICAL",L"车体危急",L"車體危急",L"車体損傷大")
HUDTEXT(drillOverheat,L"DRILL OVERHEAT",L"钻头过热",L"鑽頭過熱",L"ドリル過熱")
HUDTEXT(noAmmo,L"NO AMMO",L"弹药耗尽",L"彈藥耗盡",L"弾切れ")
HUDTEXT(stockGunner,L"%hs  GUNNER %u",L"%hs  炮手 %u",L"%hs  砲手 %u",L"%hs  砲手 %u")
HUDTEXT(stockInfo,L"SPD %d km/h    HP %d%%",L"速度 %d km/h    耐久 %d%%",L"速度 %d km/h    耐久 %d%%",L"速度 %d km/h    耐久 %d%%")
HUDTEXT(twist,L"TWIST %+d",L"扭转 %+d",L"扭轉 %+d",L"ひねり %+d")
HUDTEXT(stabLag,L"STAB LAG",L"稳定器滞后",L"穩定器滯後",L"スタビ遅れ")
HUDTEXT(stab,L"STAB",L"稳定器",L"穩定器",L"スタビ")
// The stock armor gauge's numbers where it is hidden (stockgauge.cpp, hud.cpp ArmorRows): the player's armor, and in a
// vehicle whose HUD shows no durability of its own (the aircraft, the Sazabi) the vehicle's.
HUDTEXT(armorRow,L"ARMOR %d / %d",L"护甲 %d / %d",L"護甲 %d / %d",L"アーマー %d / %d")
HUDTEXT(hullRow,L"HULL %d / %d",L"耐久 %d / %d",L"耐久 %d / %d",L"耐久 %d / %d")

// --- The map (map.cpp, mapcmd.cpp) ---
HUDTEXT(orderFollow,L"FOLLOW",L"跟随",L"跟隨",L"追従")
HUDTEXT(orderGuard,L"GUARD",L"守点",L"守點",L"防衛")
// RELEASE: the unit's order dropped, back to what it did before any (not "dismiss": 2026-10-09 the user took the two
// for one, "解散解除…是不是重叠了").
HUDTEXT(orderRelease,L"CLEAR ORDER",L"取消命令",L"取消命令",L"命令取消")
HUDTEXT(selectedAll,L"SELECTED ALL %d",L"已选全部 %d",L"已選全部 %d",L"全員選択 %d")
HUDTEXT(selectedOne,L"SELECTED 1 / %d: %ls",L"已选 1 / %d：%ls",L"已選 1 / %d：%ls",L"選択 1 / %d：%ls")
HUDTEXT(selectedSome,L"SELECTED %d / %d",L"已选 %d / %d",L"已選 %d / %d",L"選択 %d / %d")
HUDTEXT(npcOfflineOnly,L"NPC COMMANDS: OFFLINE ONLY",L"NPC 指挥：仅限离线",L"NPC 指揮：僅限離線",L"NPC 指揮：オフライン専用")
HUDTEXT(npcNone,L"NPC COMMANDS: no NPC unit (squads, helis, jets, crawlers, tanks)",L"NPC 指挥：没有 NPC 单位（小队、直升机、喷气机、爬行者、坦克）",
        L"NPC 指揮：沒有 NPC 單位（小隊、直升機、噴射機、爬行者、坦克）",L"NPC 指揮：ユニットなし（分隊・ヘリ・ジェット・クロウラー・戦車）")
HUDTEXT(npcPadKeys,L"%ls   X select (ALL last)   Y guard crosshair   RB follow me   LB release",
        L"%ls   X 选择（最后为全部）   Y 守准星处   RB 跟随我   LB 解除",L"%ls   X 選擇（最後為全部）   Y 守準星處   RB 跟隨我   LB 解除",
        L"%ls   X 選択（最後は全員）   Y 照準点を防衛   RB 自分に追従   LB 解除")
HUDTEXT(npcMouseKeys,L"%ls   RIGHT-CLICK move / attack   Z attack-move   G guard   V follow   X release",
        L"%ls   右键 移动 / 攻击   Z 攻击移动   G 守点   V 跟随我   X 解除",
        L"%ls   右鍵 移動 / 攻擊   Z 攻擊移動   G 守點   V 跟隨我   X 解除",
        L"%ls   右クリック 移動 / 攻撃   Z 攻撃移動   G 防衛   V 追従   X 解除")
HUDTEXT(mapTitle,L"MAP   height %ls   grid %ls%ls",L"地图   高度 %ls   网格 %ls%ls",L"地圖   高度 %ls   網格 %ls%ls",L"マップ   高度 %ls   グリッド %ls%ls")
HUDTEXT(mapPadKeys,L"L stick pan   R stick turn / tilt   RT / LT zoom   A centre   B / map button close",
        L"左摇杆 平移   右摇杆 旋转 / 俯仰   RT / LT 缩放   A 居中   B / 地图键 关闭",L"左搖桿 平移   右搖桿 旋轉 / 俯仰   RT / LT 縮放   A 置中   B / 地圖鍵 關閉",
        L"左スティック 移動   右スティック 回転 / 傾き   RT / LT ズーム   A 中央へ   B / マップボタン 閉じる")
HUDTEXT(mapMouseKeys,L"LMB drag / WASD pan   RMB drag / Q E turn   R F tilt   wheel / + - zoom   Space centre   %ls / Esc close",
        L"左键拖动 / WASD 平移   右键拖动 / Q E 旋转   R F 俯仰   滚轮 / + - 缩放   空格 居中   %ls / Esc 关闭",
        L"左鍵拖曳 / WASD 平移   右鍵拖曳 / Q E 旋轉   R F 俯仰   滾輪 / + - 縮放   空白鍵 置中   %ls / Esc 關閉",
        L"左ドラッグ / WASD 移動   右ドラッグ / Q E 回転   R F 傾き   ホイール / + - ズーム   スペース 中央へ   %ls / Esc 閉じる")
HUDTEXT(legendYou,L"YOU",L"你",L"你",L"自分")
HUDTEXT(legendSquad,L"SQUAD",L"小队",L"小隊",L"分隊")
HUDTEXT(legendFriendly,L"FRIENDLY",L"友军",L"友軍",L"味方")
HUDTEXT(legendVehicle,L"VEHICLE",L"载具",L"載具",L"ビークル")
HUDTEXT(legendAircraft,L"AIRCRAFT",L"飞机",L"飛機",L"航空機")
HUDTEXT(legendHelicopter,L"HELICOPTER",L"直升机",L"直升機",L"ヘリコプター")
HUDTEXT(legendEmpty,L"EMPTY (NO CREW)",L"空载具（无人）",L"空載具（無人）",L"空き（無人）")
HUDTEXT(legendCarrier,L"CARRIER",L"母舰",L"母艦",L"母艦")
HUDTEXT(legendLargeEnemy,L"LARGE ENEMY",L"大型敌人",L"大型敵人",L"大型の敵")
HUDTEXT(legendLargeEnemyAir,L"LARGE ENEMY AIR",L"大型空中敌人",L"大型空中敵人",L"大型の敵（空中）")
HUDTEXT(legendObjective,L"OBJECTIVE",L"目标",L"目標",L"目標")
HUDTEXT(legendEnemy,L"ENEMY",L"敌人",L"敵人",L"敵")
HUDTEXT(legendEnemyFlying,L"ENEMY FLYING",L"飞行的敌人",L"飛行的敵人",L"飛行する敵")
HUDTEXT(legendLock,L"LOCK",L"锁定",L"鎖定",L"ロック")
HUDTEXT(legendNearestEnemy,L"NEAREST ENEMY",L"最近的敌人",L"最近的敵人",L"最寄りの敵")
HUDTEXT(cmdOfflineOnly,L"COMMANDS: OFFLINE ONLY",L"指挥：仅限离线",L"指揮：僅限離線",L"指揮：オフライン専用")
HUDTEXT(cmdSelectFirst,L"SELECT UNITS FIRST (%ls)",L"请先选择单位（%ls）",L"請先選擇單位（%ls）",L"先にユニットを選択（%ls）")
HUDTEXT(cmdNoUnit,L"NO UNIT TO COMMAND",L"没有可指挥的单位",L"沒有可指揮的單位",L"指揮できるユニットなし")
HUDTEXT(cmdSelectHowMouse,L"CTRL+DRAG / CLICK / TAB",L"Ctrl+拖动 / 点击 / Tab",L"Ctrl+拖曳 / 點擊 / Tab",L"Ctrl+ドラッグ / クリック / Tab")
HUDTEXT(cmdNoGround,L"NO GROUND THERE",L"那里没有地面",L"那裡沒有地面",L"そこに地面がない")
HUDTEXT(cmdGuardOne,L"%ls (%.0f, %.0f): %d UNIT",L"%ls（%.0f, %.0f）：%d 个单位",L"%ls（%.0f, %.0f）：%d 個單位",L"%ls（%.0f, %.0f）：%d ユニット")
HUDTEXT(cmdGuardMany,L"%ls (%.0f, %.0f): %d UNITS",L"%ls（%.0f, %.0f）：%d 个单位",L"%ls（%.0f, %.0f）：%d 個單位",L"%ls（%.0f, %.0f）：%d ユニット")
HUDTEXT(cmdGivenOne,L"%ls: %d UNIT",L"%ls：%d 个单位",L"%ls：%d 個單位",L"%ls：%d ユニット")
HUDTEXT(cmdGivenMany,L"%ls: %d UNITS",L"%ls：%d 个单位",L"%ls：%d 個單位",L"%ls：%d ユニット")
HUDTEXT(unitHeli,L"HELI %ls",L"直升机 %ls",L"直升機 %ls",L"ヘリ %ls")
HUDTEXT(unitJet,L"JET %ls",L"%ls",L"%ls",L"%ls")

// --- Words for the run-time identifiers (hudtext.h kWords) ---
HUDTEXT(wordGun,L"GUN",L"机炮",L"機砲",L"機関砲")
HUDTEXT(wordCannon,L"CANNON",L"加农炮",L"加農砲",L"キャノン")
HUDTEXT(wordMissile,L"MSL",L"飞弹",L"飛彈",L"ミサイル")
HUDTEXT(wordRocket,L"RKT",L"火箭弹",L"火箭彈",L"ロケット")
HUDTEXT(wordGrenade,L"GREN",L"榴弹",L"榴彈",L"グレネード")
HUDTEXT(wordLaser,L"LASER",L"激光",L"雷射",L"レーザー")
HUDTEXT(wordHomingLaser,L"HLASER",L"追踪激光",L"追蹤雷射",L"誘導レーザー")
HUDTEXT(wordMaser,L"ATOMIC RAY",L"原子光线炮",L"原子光線砲",L"原子光線砲")
HUDTEXT(wordBeam,L"BEAM",L"光束",L"光束",L"ビーム")
HUDTEXT(wordFlame,L"FLAME",L"火焰",L"火焰",L"火炎")
HUDTEXT(wordAcid,L"ACID",L"酸液",L"酸液",L"酸")
HUDTEXT(wordNapalm,L"NAPALM",L"燃烧弹",L"燃燒彈",L"ナパーム")
HUDTEXT(wordWeapon,L"WPN",L"武器",L"武器",L"武器")
HUDTEXT(wordRocketPods,L"ROCKETS",L"火箭炮",L"火箭砲",L"ロケット砲")
HUDTEXT(kindStrike,L"strike",L"攻击机",L"攻擊機",L"攻撃機")
HUDTEXT(kindFighter,L"fighter",L"战斗机",L"戰鬥機",L"戦闘機")
HUDTEXT(kindInterceptor,L"interceptor",L"截击机",L"截擊機",L"迎撃機")
HUDTEXT(kindMultirole,L"multirole",L"多用途机",L"多用途機",L"マルチロール機")
HUDTEXT(kindCarrier,L"carrier",L"无人机母舰",L"無人機母艦",L"ドローン母艦")
HUDTEXT(kindDrone,L"drone",L"无人机",L"無人機",L"ドローン")
HUDTEXT(kindBlast,L"blast",L"自爆无人机",L"自爆無人機",L"自爆ドローン")
HUDTEXT(kindDoll,L"doll",L"人偶无人机",L"人偶無人機",L"人形ドローン")
HUDTEXT(kindGunship,L"gunship",L"炮舰机",L"砲艦機",L"ガンシップ")
HUDTEXT(kindPrimer,L"primer",L"星导者",L"星導者",L"プライマー")
HUDTEXT(kindCentipede,L"centipede",L"百足龙虫",L"百足龍蟲",L"百足竜虫")
HUDTEXT(kindDragonfly,L"dragonfly",L"蜻蜓空优机",L"蜻蜓空優機",L"トンボ制空機")
HUDTEXT(kindJet,L"jet",L"喷气机",L"噴射機",L"ジェット機")
HUDTEXT(kindHeli,L"heli",L"直升机",L"直升機",L"ヘリ")
HUDTEXT(kindHeliBase,L"base",L"通用型",L"通用型",L"汎用型")
HUDTEXT(kindCrawler,L"crawler",L"深渊爬行者",L"深淵爬行者",L"デプス・クロウラー")
HUDTEXT(kindDrill,L"drill",L"钻头战车",L"鑽頭戰車",L"ドリル戦車")
HUDTEXT(unitCrawler,L"CRAWLER",L"深渊爬行者",L"深淵爬行者",L"デプス・クロウラー")
HUDTEXT(unitMech,L"MECH",L"机甲",L"機甲",L"メカ")
HUDTEXT(kindNpc,L"npc",L"NPC",L"NPC",L"NPC")

HUDTEXT(areaTurning,L"AREA EDGE: TURNING BACK",L"接近边界：正在返航",L"接近邊界：正在返航",L"エリア境界：旋回中")
HUDTEXT(areaAhead,L"AREA EDGE AHEAD: TURN BACK",L"前方边界：请返航",L"前方邊界：請返航",L"前方エリア境界：引き返せ")
HUDTEXT(warnArea,L"AREA EDGE",L"区域边界",L"區域邊界",L"エリア境界")
HUDTEXT(boardingEntry,L"BOARDING ENTRANCE  %d m",L"登乘入口  %d m",L"登乘入口  %d m",L"搭乗口  %d m")
HUDTEXT(boardingReady,L"BOARD AT THIS ENTRANCE",L"在此按上车键",L"在此按上車鍵",L"ここで搭乗ボタン")
HUDTEXT(boardingHail,L"[%ls] CALL IT DOWN  %d m",L"[%ls] 呼叫降落  %d m",L"[%ls] 呼叫降落  %d m",L"[%ls] 着陸要請  %d m")
HUDTEXT(boardingHailing,L"CALLED: LANDING  %d m",L"已呼叫：正在降落  %d m",L"已呼叫：正在降落  %d m",L"要請済み：着陸中  %d m")

// Contextual aircraft weapon controls (physical bindings supplied by the flight / payload readout).
HUDTEXT(controlUnbound,L"unbound",L"未绑定",L"未綁定",L"未割当")
HUDTEXT(controlStores,L"[%ls] stores",L"[%ls] 切换挂载",L"[%ls] 切換掛載",L"[%ls] 兵装切替")
HUDTEXT(controlTarget,L"[%ls] next target",L"[%ls] 切换目标",L"[%ls] 切換目標",L"[%ls] 目標切替")
HUDTEXT(controlFlares,L"[%ls] flares",L"[%ls] 热诱弹",L"[%ls] 熱誘彈",L"[%ls] フレア")
HUDTEXT(controlFlaresKeyboard,L"[keyboard %ls] flares",L"[键盘 %ls] 热诱弹",L"[鍵盤 %ls] 熱誘彈",L"[キーボード %ls] フレア")

// NPC squads and their map orders; identifiers are localized through hudtext::Word.
HUDTEXT(virtualKey,L"VK 0x%X",L"键码 0x%X",L"鍵碼 0x%X",L"キーコード 0x%X")
HUDTEXT(orderEngage,L"ENGAGE AT WILL",L"自由交战",L"自由交戰",L"自由交戦")
HUDTEXT(orderFocus,L"FOCUS FIRE",L"集火",L"集火",L"集中攻撃")
HUDTEXT(orderBoard,L"BOARD",L"上车",L"上車",L"搭乗")
HUDTEXT(orderDismount,L"DISMOUNT",L"下车",L"下車",L"降車")
HUDTEXT(orderDismiss,L"DISMISS",L"离队",L"離隊",L"離脱")
HUDTEXT(orderRecruit,L"RECRUIT",L"招募",L"招募",L"加入指示")
// The RTS moves (mapcmd_logic.h Order::move / attackMove; hud.cpp MapButtons, mapcmd.cpp).
HUDTEXT(orderMove,L"MOVE",L"移动",L"移動",L"移動")
HUDTEXT(orderAttackMove,L"ATTACK-MOVE",L"攻击移动",L"攻擊移動",L"攻撃移動")
// The transports (transport.cpp; mapcmd_logic.h Order::withdraw).
HUDTEXT(orderWithdraw,L"SEND TRANSPORT AWAY",L"载具撤离",L"載具撤離",L"輸送車両撤収")
HUDTEXT(cmdNpcNoTransport,L"No transport to withdraw (only a support vehicle leaves; get the squad off a moving one first)",
        L"没有可撤离的运输载具（只有支援载具能撤离；载具行进中请先让小队下车）",L"沒有可撤離的運輸載具（只有支援載具能撤離；載具行進中請先讓小隊下車）",
        L"撤収できる輸送車両がない（支援車両のみ；走行中は先に降車させる）")
HUDTEXT(cmdOrderArmed,L"%ls: LEFT-CLICK THE MAP (RIGHT-CLICK: CANCEL)",L"%ls：左键点击地图指定位置（右键取消）",L"%ls：左鍵點擊地圖指定位置（右鍵取消）",
        L"%ls：マップを左クリックで地点指定（右クリックで取消）")
HUDTEXT(cmdSupportArmed,L"%ls: LEFT-CLICK THE MAP FOR ITS DESTINATION (RIGHT-CLICK: CANCEL)",L"%ls：左键点击地图指定目的地（右键取消）",
        L"%ls：左鍵點擊地圖指定目的地（右鍵取消）",L"%ls：マップを左クリックで目的地指定（右クリックで取消）")
HUDTEXT(cmdArmCancelled,L"CANCELLED",L"已取消",L"已取消",L"取消しました")
HUDTEXT(cmdNpcRiding,L"This squad is aboard a vehicle: command its vehicle (or order it to dismount)",L"该小队在载具上：请指挥它的载具（或先下令下车）",
        L"該小隊在載具上：請指揮它的載具（或先下令下車）",L"この分隊は搭乗中：車両に命令する（または降車させる）")
HUDTEXT(npcRiding,L"RIDING",L"乘车中",L"乘車中",L"搭乗中")
// The command card's tooltips and its hint with nothing selected (hud.cpp MapButtons).
HUDTEXT(tipKey,L"%ls   key %ls",L"%ls   按键 %ls",L"%ls   按鍵 %ls",L"%ls   キー %ls")
HUDTEXT(tipRightClick,L"%ls   RIGHT-CLICK THE MAP (an enemy: attack it)",L"%ls   右键点击地图（点敌人：攻击它）",L"%ls   右鍵點擊地圖（點敵人：攻擊它）",
        L"%ls   マップを右クリック（敵なら攻撃）")
HUDTEXT(tipArms,L"%ls   key %ls; the button: then left-click the map",L"%ls   按键 %ls；点按钮后左键点击地图",L"%ls   按鍵 %ls；點按鈕後左鍵點擊地圖",
        L"%ls   キー %ls、ボタンの後マップを左クリック")
HUDTEXT(btnFormationShort,L"FORMATION",L"编队",L"編隊",L"隊形")
HUDTEXT(btnHealth,L"HEALTH BOXES",L"回复箱",L"回復箱",L"回復箱")
HUDTEXT(cmdHintSelect,L"LEFT-CLICK A UNIT, CTRL+DRAG A BOX OR TAB; THEN RIGHT-CLICK TO MOVE OR ATTACK",
        L"左键点选单位、Ctrl+拖动框选或 Tab 轮换；然后右键移动或攻击",L"左鍵點選單位、Ctrl+拖曳框選或 Tab 輪換；然後右鍵移動或攻擊",
        L"左クリックで選択、Ctrl+ドラッグで範囲選択、Tab で切替。右クリックで移動・攻撃")
// The support bar (hud.cpp MapSupportBar): its title, the dispatcher's state, a row's tooltip.
HUDTEXT(supportTitle,L"SUPPORT",L"支援",L"支援",L"支援")
HUDTEXT(supportPlanning,L"PLANNING",L"调度中",L"調度中",L"手配中")
HUDTEXT(supportCooldown,L"%d s",L"冷却 %d 秒",L"冷卻 %d 秒",L"待機 %d 秒")
HUDTEXT(supportOff,L"OFF",L"不可用",L"不可用",L"使用不可")
HUDTEXT(supportTip,L"%ls   click, then left-click the map   [ ] pick, C call at the pointer",L"%ls   点击后左键点击地图   [ ] 切换，C 在指针处呼叫",
        L"%ls   點擊後左鍵點擊地圖   [ ] 切換，C 在指標處呼叫",L"%ls   クリック後マップを左クリック   [ ] 切替、C ポインター位置に要請")
HUDTEXT(legendTitle,L"LEGEND",L"图例",L"圖例",L"凡例")
HUDTEXT(cmdNoMark,L"NO MARK (Q, OR POINT AT AN ENEMY)",L"没有标记（按 Q，或把指针放到敌人上）",L"沒有標記（按 Q，或把指標放到敵人上）",
        L"マークなし（Q、または敵にポインターを合わせる）")
// The enemy under the map's pointer (hud.cpp MapCommands) and what the mark key did with it (mapcmd.cpp).
HUDTEXT(cmdHoverEnemy,L"RIGHT-CLICK ATTACK   Q MARK",L"右键 攻击   Q 标记",L"右鍵 攻擊   Q 標記",L"右クリック 攻撃   Q マーク")
HUDTEXT(cmdMarked,L"ENEMY MARKED",L"已标记敌人",L"已標記敵人",L"敵をマーク")
HUDTEXT(cmdUnmarked,L"MARK CLEARED",L"已取消标记",L"已取消標記",L"マーク解除")
// The optional tail of cmdGuardResult / cmdOrderResult (their last %ls, empty when every unit took the order): the separator
// is the tail's own, so a result without one ends clean (tools/hudtext_check.cpp Tails).
HUDTEXT(cmdCannot,L", %d CANNOT",L"，%d 个无法执行",L"，%d 個無法執行",L"、%d ユニットは実行不可")
HUDTEXT(cmdGuardResult,L"%ls (%.0f, %.0f): %d UNIT(S)%ls",L"%ls（%.0f, %.0f）：%d 个单位%ls",L"%ls（%.0f, %.0f）：%d 個單位%ls",L"%ls（%.0f, %.0f）：%d ユニット%ls")
HUDTEXT(cmdOrderResult,L"%ls: %d UNIT(S)%ls",L"%ls：%d 个单位%ls",L"%ls：%d 個單位%ls",L"%ls：%d ユニット%ls")
HUDTEXT(squadTitle,L"SQUADS",L"小队",L"小隊",L"分隊")
HUDTEXT(squadTitleKeys,L"SQUADS (1-9 pick, Shift add)",L"小队（1-9 选择，Shift 追加）",L"小隊（1-9 選擇，Shift 追加）",L"分隊（1-9 選択、Shift 追加）")
// The map's command buttons (hud.cpp MapButtons; map_buttons.h).
HUDTEXT(btnFormation,L"FORMATION: %ls",L"编队：%ls",L"編隊：%ls",L"隊形：%ls")
HUDTEXT(btnSplit,L"SPLIT",L"分组",L"分組",L"分割")
HUDTEXT(btnMerge,L"JOIN",L"合并",L"合併",L"合流")
HUDTEXT(btnSweep,L"FETCH BOXES",L"拾取箱子",L"拾取箱子",L"アイテム回収")
HUDTEXT(btnSweepStop,L"CALL BACK",L"召回",L"召回",L"呼び戻し")
HUDTEXT(btnHealthOff,L"HEALTH BOXES: FOR THE PLAYER",L"回复箱：留给玩家",L"回復箱：留給玩家",L"回復箱：プレイヤー用")
HUDTEXT(btnHealthOn,L"HEALTH BOXES: HURT NPCS TOO",L"回复箱：受伤 NPC 可捡",L"回復箱：受傷 NPC 可撿",L"回復箱：負傷 NPC も")
HUDTEXT(cmdSweepOn,L"FETCHING BOXES (%d SELECTED SQUADS; 0: YOUR SQUADS)",L"拾取箱子（%d 个选中的小队；0 = 跟着你的小队）",
        L"拾取箱子（%d 個選中的小隊；0 = 跟著你的小隊）",L"アイテム回収（選択 %d 分隊、0 = 同行分隊）")
HUDTEXT(cmdSweepOff,L"CALLED BACK",L"已召回",L"已召回",L"呼び戻し")
HUDTEXT(cmdSweepUnavailable,L"BOX SWEEP UNAVAILABLE (SEE THE LOG)",L"拾取不可用（见日志）",L"拾取不可用（見日誌）",L"回収は使用不可（ログ参照）")
HUDTEXT(cmdHealthOnline,L" (NOT ONLINE)",L"（联机时不捡）",L"（連線時不撿）",L"（オンラインでは無効）")
HUDTEXT(cmdSplitResult,L"SPLIT: %d SQUAD(S), %d TOO SMALL",L"分组：%d 个小队，%d 个人数不够",L"分組：%d 個小隊，%d 個人數不夠",L"分割：%d 分隊、%d 分隊は人数不足")
HUDTEXT(cmdMergeResult,L"JOINED: %d SQUAD(S), %d CANNOT",L"合并：%d 个小队，%d 个无法合并",L"合併：%d 個小隊，%d 個無法合併",L"合流：%d 分隊、%d 分隊は不可")
// The squads' formations (formation.h Shape, in its order; npcai.cpp, mapcmd.cpp FormationText).
HUDTEXT(formStock,L"DEFAULT",L"默认",L"預設",L"標準")
HUDTEXT(formColumn,L"COLUMN",L"纵队",L"縱隊",L"縦隊")
HUDTEXT(formStaggered,L"STAGGERED COLUMN",L"交错纵队",L"交錯縱隊",L"千鳥縦隊")
HUDTEXT(formWedge,L"WEDGE",L"楔形",L"楔形",L"楔形")
HUDTEXT(formVee,L"VEE",L"V 形",L"V 形",L"V 字")
HUDTEXT(formLine,L"LINE",L"横队",L"橫隊",L"横隊")
HUDTEXT(formEchelonLeft,L"ECHELON LEFT",L"左梯队",L"左梯隊",L"左梯形")
HUDTEXT(formEchelonRight,L"ECHELON RIGHT",L"右梯队",L"右梯隊",L"右梯形")
HUDTEXT(formDiamond,L"DIAMOND",L"菱形",L"菱形",L"菱形")
HUDTEXT(formBounding,L"BOUNDING OVERWATCH",L"交替掩护",L"交替掩護",L"交互躍進")
HUDTEXT(formPerimeter,L"PERIMETER",L"环形防御",L"環形防禦",L"全周防御")
HUDTEXT(cmdFormationResult,L"FORMATION %ls: %d SQUAD(S)",L"编队 %ls：%d 个小队",L"編隊 %ls：%d 個小隊",L"隊形 %ls：%d 分隊")
HUDTEXT(sweepOn,L"FETCHING BOXES: %d LEFT, %d IN   [%ls] call back",L"拾取箱子：剩 %d 个，已捡 %d   [%ls] 召回",L"拾取箱子：剩 %d 個，已撿 %d   [%ls] 召回",
        L"アイテム回収：残り %d、回収 %d   [%ls] 呼び戻し")
HUDTEXT(sweepDone,L"BOXES FETCHED: %d",L"拾取结束：捡到 %d 个",L"拾取結束：撿到 %d 個",L"回収終了：%d 個")
HUDTEXT(formationCue,L"FORMATION: %ls   [%ls]",L"编队：%ls   [%ls]",L"編隊：%ls   [%ls]",L"隊形：%ls   [%ls]")
HUDTEXT(squadWait,L"WAIT %d s",L"等待 %d 秒",L"等待 %d 秒",L"待機 %d 秒")
// The squad panel's summary row (hud.cpp MapSquadPanel; the user, 2026-10-09: "小队太多了吧…感觉看不过来"): folded, the
// squads not listed (how many of them ride a vehicle, how many a script drives); open, the row that folds it again.
HUDTEXT(squadMore,L"+%d MORE (%d riding, %d scripted): click to list",L"另有 %d 支（乘车 %d，脚本 %d）：点击展开",
        L"另有 %d 支（乘車 %d，腳本 %d）：點擊展開",L"他 %d 部隊（搭乗 %d、指示中 %d）：クリックで表示")
// The formation menu's two sections (hud.cpp MapButtons): the march of the squads following the player, the defence of
// the ones on a guard point.
HUDTEXT(menuMarch,L"MARCH",L"行军",L"行軍",L"行軍")
HUDTEXT(menuDefence,L"DEFENCE",L"守点",L"守點",L"防衛")
HUDTEXT(squadFold,L"Click to fold: only the squads that take orders",L"点击收起：只列可指挥的小队",L"點擊收起：只列可指揮的小隊",
        L"クリックでたたむ：指揮できる分隊のみ")
HUDTEXT(npcMark,L"MARK",L"标记",L"標記",L"マーク")
HUDTEXT(npcMarkRange,L"MARK %.0f m",L"标记 %.0f m",L"標記 %.0f m",L"マーク %.0f m")
// The mark key on foot with no enemy near the centre (npcai.cpp SendToPoint): the units selected on the map sent there.
HUDTEXT(npcPingSent,L"MOVE HERE: %d UNIT(S)",L"前往此处：%d 个单位",L"前往此處：%d 個單位",L"ここへ移動：%d ユニット")
HUDTEXT(npcPingNearEnemy,L"AIM AT THE ENEMY TO MARK IT",L"对准敌人才能标记",L"對準敵人才能標記",L"敵に合わせるとマーク")
HUDTEXT(npcPingNoUnit,L"NO ENEMY HERE (SELECT UNITS ON THE MAP TO SEND THEM)",L"这里没有敌人（在地图上选择单位后可派往此处）",
        L"這裡沒有敵人（在地圖上選擇單位後可派往此處）",L"ここに敵はいない（マップでユニットを選ぶとここへ送れる）")
HUDTEXT(npcRanger,L"RANGER",L"游骑兵",L"遊騎兵",L"レンジャー")
HUDTEXT(npcWingDiver,L"WING DIVER",L"飞翼潜兵",L"飛翼潛兵",L"ウイングダイバー")
HUDTEXT(npcFencer,L"FENCER",L"剑兵",L"劍兵",L"フェンサー")
HUDTEXT(npcAirRaider,L"AIR RAIDER",L"空袭兵",L"空襲兵",L"エアレイダー")
HUDTEXT(npcRecruited,L"RECRUITED",L"已招募",L"已招募",L"加入済み")
HUDTEXT(npcFree,L"FREE",L"自由行动",L"自由行動",L"自由行動")
HUDTEXT(npcScript,L"SCRIPT",L"剧情控制",L"劇情控制",L"シナリオ制御")
HUDTEXT(npcHold,L"HOLD",L"驻守",L"駐守",L"待機")
HUDTEXT(npcEscort,L"ESCORT",L"护送",L"護送",L"護衛")

// --- The Sazabi (hud.cpp SazabiPanel; README 沙扎比) ---
HUDTEXT(sazabiTitle,L"SAZABI",L"沙扎比",L"沙薩比",L"サザビー")
HUDTEXT(sazabiThruster,L"THRUSTER",L"推进器",L"推進器",L"スラスター")
HUDTEXT(sazabiRifle,L"BEAM RIFLE",L"光束步枪",L"光束步槍",L"ビームライフル")
HUDTEXT(sazabiMissiles,L"SHIELD MISSILE %d",L"护盾飞弹 %d",L"護盾飛彈 %d",L"シールドミサイル %d")
HUDTEXT(sazabiCannon,L"MEGA PARTICLE CANNON",L"米加粒子炮",L"米加粒子砲",L"メガ粒子砲")
HUDTEXT(sazabiFunnels,L"FUNNELS",L"浮游炮",L"浮游砲",L"ファンネル")
HUDTEXT(sazabiGuard,L"GUARD",L"防御",L"防禦",L"ガード")
HUDTEXT(sazabiAir,L"AIR",L"空中",L"空中",L"空中")
HUDTEXT(sazabiTomahawk,L"TOMAHAWK",L"战斧",L"戰斧",L"トマホーク")
HUDTEXT(sazabiAltitude,L"ALT %d m",L"高度 %d m",L"高度 %d m",L"高度 %d m")
HUDTEXT(sazabiSpeed,L"SPD %d km/h",L"速度 %d km/h",L"速度 %d km/h",L"速度 %d km/h")
HUDTEXT(sazabiSwitch,L"SWITCH [%ls]",L"切换 [%ls]",L"切換 [%ls]",L"切替 [%ls]")
HUDTEXT(sazabiLock,L"LOCK",L"锁定",L"鎖定",L"ロック")
HUDTEXT(cmdSupportPicked,L"SUPPORT: %ls",L"支援：%ls",L"支援：%ls",L"支援：%ls")
HUDTEXT(cmdSupportNoPoint,L"No support destination found",L"无法确定支援目的地",L"無法確定支援目的地",L"支援先を確認できません")
HUDTEXT(cmdPayloadQueued,L"Weapon selection queued; the weapon panel shows the applied choice",L"挂载选择已提交，以武器面板的选中状态为准",L"掛載選擇已提交，以武器面板的選中狀態為準",L"武装選択を受付、反映結果は武器パネルで確認")
HUDTEXT(cmdPayloadStale,L"Weapon selection changed or is unavailable; select again",L"挂载状态已变化或不可选择，请重新选择",L"掛載狀態已變化或不可選擇，請重新選擇",L"武装状態が変化したか選択できません。再選択してください")
HUDTEXT(cmdNpcDisabled,L"NPC command control is disabled or unavailable",L"NPC 指挥功能未启用或尚未就绪",L"NPC 指揮功能未啟用或尚未就緒",L"NPC 指揮機能が無効か準備未完了です")
HUDTEXT(cmdNpcRequester,L"No valid player is associated with this command",L"此命令没有有效的玩家身份",L"此命令沒有有效的玩家身分",L"有効なプレイヤーを確認できません")
HUDTEXT(cmdNpcStale,L"The selected squad changed or left; select it again",L"所选小队已变化或离开，请重新选择",L"所選小隊已變化或離開，請重新選擇",L"選択した部隊が変化しました。再選択してください")
HUDTEXT(cmdNpcAuthority,L"This unit must be commanded by its AI authority",L"该单位需要由真正控制其 AI 的机器执行命令",L"該單位需要由真正控制其 AI 的主機執行命令",L"この部隊の AI を管理する端末で実行する必要があります")
HUDTEXT(cmdNpcOwner,L"This squad follows another player",L"该小队属于另一名玩家，不能接管",L"該小隊屬於另一名玩家，不能接管",L"この部隊は別のプレイヤーに従っています")
HUDTEXT(cmdNpcScript,L"The mission script currently controls this squad",L"任务脚本正在控制该小队，不能接管",L"任務腳本正在控制該小隊，不能接管",L"この部隊はミッションの指示に従っています")
HUDTEXT(cmdNpcFriendly,L"Only friendly squads can receive these orders",L"只能向友方小队下达命令",L"只能向友方小隊下達命令",L"味方部隊にのみ命令できます")
HUDTEXT(cmdNpcCooldown,L"This dismissed squad is still on recruitment cooldown",L"该小队仍在遣散后的招募冷却中",L"該小隊仍在遣散後的招募冷卻中",L"解散後の再加入待機時間中です")
HUDTEXT(cmdNpcTarget,L"The target is invalid or no reachable ground was found",L"目标已失效或找不到对应地面，请重新指定",L"目標已失效或找不到對應地面，請重新指定",L"目標が無効か地面を確認できません。再指定してください")
HUDTEXT(cmdNpcNoVehicle,L"No friendly vehicle with an entry within 150 m",L"150 米内没有可登乘的友方载具入口",L"150 公尺內沒有可登乘的友方載具入口",L"150 m 以内に搭乗可能な味方車両の入口がありません")
HUDTEXT(cmdNpcNoSeat,L"No free compatible seat for the available squad members",L"没有适合可行动队员的空闲座位",L"沒有適合可行動隊員的空閒座位",L"行動可能な隊員が搭乗できる空席がありません")
HUDTEXT(cmdNpcBoardOff,L"NPC boarding is disabled or its native entry is unavailable",L"NPC 登车未启用或原版登车接口不可用",L"NPC 登車未啟用或原版登車介面不可用",L"NPC 搭乗が無効か搭乗機能を利用できません")
HUDTEXT(cmdNpcUnsupported,L"This unit or state cannot execute this order; select its vehicle if aboard",L"此单位或状态不支持该命令；已乘车时请选载具",L"此單位或狀態不支援該命令；已乘車時請選載具",L"この状態では実行できません。搭乗中は車両を選択してください")
HUDTEXT(cmdNpcFailed,L"The command was not accepted; refresh the selection",L"命令未被接受，请刷新选择后重试",L"命令未被接受，請重新選擇後再試",L"命令を受け付けませんでした。再選択してください")
HUDTEXT(cmdBoardAssigned,L"Boarding assigned to %u soldiers; they will walk to their seats%ls",L"已为 %u 名队员分配登车，他们将走到座位入口%ls",L"已為 %u 名隊員分配登車，他們將走到座位入口%ls",L"%u 人に搭乗を指示、座席の入口まで移動します%ls")
HUDTEXT(cmdNpcNetworkQueued,L"%ls: requested authority for %u squads; %d local units accepted",L"%ls：已请求权威端处理 %u 个小队，本机已受理 %d 个单位",L"%ls：已請求權威端處理 %u 個小隊，本機已受理 %d 個單位",L"%ls：%u 部隊を管理端末に要求、本機は %d 部隊を受付")
HUDTEXT(cmdNpcNetworkDone,L"%ls: authority accepted %u/%u squads, %u soldiers. %ls",L"%ls：权威端已受理 %u/%u 个小队，涉及 %u 名队员。%ls",L"%ls：權威端已受理 %u/%u 個小隊，涉及 %u 名隊員。%ls",L"%ls：管理端末が %u/%u 部隊、%u 人を受付。%ls")
HUDTEXT(cmdNpcNetworkTimeout,L"The authority did not confirm this command in time",L"权威端未及时确认这条命令，请检查联机后重试",L"權威端未及時確認這條命令，請檢查連線後再試",L"管理端末の確認が時間内に届きませんでした")
HUDTEXT(cmdNpcNetworkInterrupted,L"This command lost its mission/session context and was interrupted",L"命令的本关或会话身份已变化，请重新选择后下达",L"命令的本關或工作階段身分已變化，請重新選擇後下達",L"ミッションか接続状態が変化しました。再選択してください")
HUDTEXT(cmdNpcNetworkUnavailable,L"Authority command transport is not available yet",L"权威端联机指挥尚未就绪，请确认双方版本与本关同步",L"權威端連線指揮尚未就緒，請確認雙方版本與本關同步",L"通信指揮の準備が未完了です。バージョンと同期を確認してください")
HUDTEXT(cmdNpcNetworkLimit,L"Select at most 16 remote squads; no partial batch was sent",L"一次最多选择 16 个远端小队，本次未截断发送",L"一次最多選擇 16 個遠端小隊，本次未截斷傳送",L"遠隔部隊は一度に16部隊までです。一部だけの送信は行いません")
HUDTEXT(supportInfantryTransferred,L"Infantry support is now under its new squad orders",L"步兵支援已交由新的小队命令控制",L"步兵支援已交由新的小隊命令控制",L"歩兵支援は新しい部隊命令に従います")
HUDTEXT(supportLegacyPeers,L"Support: an older All Forces in the room, so rifles and the call's own aircraft count this time",L"支援：房间里有旧版全军出击，本次兵员用步枪、飞机按默认架数（武器/架数配置未同步）",L"支援：房間裡有舊版全軍出擊，本次兵員用步槍、飛機按預設架數（武器/架數設定未同步）",L"支援：旧版の全軍出撃の参加者がいるため、今回は小銃と既定の機数で出撃します")
// --- The support bar's composition panel (hud.cpp MapComposePanel; the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人") ---
HUDTEXT(supportLoadoutLegacyPeers,L"Support: an older All Forces in the room, so the call's own soldiers this time (the chosen load was not sent)",L"支援：房间里有旧版全军出击，本次按默认兵员出动（自选编组未同步）",L"支援：房間裡有舊版全軍出擊，本次按預設兵員出動（自選編組未同步）",L"支援：旧版の全軍出撃の参加者がいるため、今回は既定の兵員で出撃します（編成は同期されません）")
HUDTEXT(supportLoadoutTooMany,L"More soldiers than this support has seats",L"兵员超过该支援的座位数",L"兵員超過該支援的座位數",L"兵員がこの支援の座席数を超えています")
HUDTEXT(composeSeats,L"%ls  seats %d / %d",L"%ls  座位 %d / %d",L"%ls  座位 %d / %d",L"%ls  座席 %d / %d")
HUDTEXT(composeHint,L"Click a kind to add a squad, a squad to take it out; left click the map to send",L"点兵种加一个小队，点小队移除；左键点地图投送",L"點兵種加一個小隊，點小隊移除；左鍵點地圖投送",L"兵種をクリックで分隊を追加、分隊をクリックで外す。マップを左クリックで投入")
HUDTEXT(composeAddTip,L"%ls squad: add one (%d seats; %d left)",L"%ls小队：加一个（占 %d 座，剩 %d 座）",L"%ls小隊：加一個（佔 %d 座，剩 %d 座）",L"%ls分隊：追加（%d 席使用、残り %d 席）")
HUDTEXT(composeFullTip,L"%ls squad: no room (a squad takes %d seats)",L"%ls小队：座位不够（一个小队占 %d 座）",L"%ls小隊：座位不夠（一個小隊佔 %d 座）",L"%ls分隊：座席不足（1 分隊 %d 席）")
HUDTEXT(composeSquadTip,L"Squad %d: leader %ls, members %ls, %d soldiers (click to take it out)",L"小队 %d：队长 %ls，队员 %ls，共 %d 人（点击移除）",L"小隊 %d：隊長 %ls，隊員 %ls，共 %d 人（點擊移除）",L"分隊 %d：隊長 %ls、隊員 %ls、計 %d 名（クリックで外す）")
HUDTEXT(composeSquad,L"%ls x%d",L"%ls×%d",L"%ls×%d",L"%ls×%d")
HUDTEXT(classRanger,L"Ranger",L"游骑兵",L"遊騎兵",L"レンジャー")
HUDTEXT(classWingDiver,L"Wing Diver",L"翼人",L"翼人",L"ウイングダイバー")
HUDTEXT(classFencer,L"Fencer",L"重装",L"重裝",L"フェンサー")
HUDTEXT(kindRifle,L"Rifle",L"步枪",L"步槍",L"小銃")
HUDTEXT(kindFlame,L"Flame",L"火焰",L"火焰",L"火炎")
HUDTEXT(kindRocket,L"Rocket",L"火箭",L"火箭",L"ロケット")
HUDTEXT(kindShotgun,L"Shotgun",L"霰弹",L"霰彈",L"散弾")
HUDTEXT(kindSniper,L"Sniper",L"狙击",L"狙擊",L"狙撃")
HUDTEXT(kindLance,L"Lance",L"长矛",L"長矛",L"ランス")
HUDTEXT(kindLaser,L"Laser",L"激光",L"雷射",L"レーザー")
HUDTEXT(kindMonster,L"Monster",L"狙击",L"狙擊",L"モンスター")
HUDTEXT(kindIzuna,L"Izuna",L"雷链",L"雷鏈",L"イズナ")
HUDTEXT(kindThunderBow,L"T.Bow",L"雷弓",L"雷弓",L"サンダーボウ")
HUDTEXT(kindCannon,L"Cannon",L"重炮",L"重砲",L"キャノン")
HUDTEXT(kindMiddleCannon,L"M.Cannon",L"中炮",L"中砲",L"中型キャノン")
HUDTEXT(kindPileBanker,L"Pile",L"打桩",L"打樁",L"パイル")
HUDTEXT(kindFencerShotgun,L"F.Shotgun",L"霰弹",L"霰彈",L"散弾")
HUDTEXT(supportAirNeedsUpdate,L"Air support: everyone in the room needs this All Forces version (it flies in from the edge now)",L"空中支援：房间里有旧版全军出击，需全员更新到同一版本（现在改为从场外空中飞入）",L"空中支援：房間裡有舊版全軍出擊，需全員更新到同一版本（現在改為從場外空中飛入）",L"航空支援：旧版の全軍出撃の参加者がいます。全員同じ版に更新してください（場外から飛来する方式になりました）")
HUDTEXT(supportInfantryArrived,L"Infantry support has arrived and is guarding the destination",L"步兵支援已抵达并守卫目的地",L"步兵支援已抵達並守衛目的地",L"歩兵支援が到着し目的地を警戒しています")

HUDTEXT(mapPayloadTitle,L"LOADOUT - click bright rows to select",L"挂载：点击亮色条目切换",L"掛載：點擊亮色項目切換",L"兵装：明るい行をクリックして選択")
HUDTEXT(predictedGround,L"GROUND %.0f m  %.1f s",L"预测地形落点 %.0f 米  %.1f 秒",L"預測地形落點 %.0f 公尺  %.1f 秒",L"予測地形着弾 %.0f m  %.1f 秒")
HUDTEXT(noGroundHit,L"NO TERRAIN HIT  %.0f m  %.1f s",L"无地形命中  %.0f 米  %.1f 秒",L"無地形命中  %.0f 公尺  %.1f 秒",L"地形着弾なし  %.0f m  %.1f 秒")

HUDTEXT(aimSightBinding,L"  SIGHT %ls / AUTO %ls",L"  瞄具 %ls / 自瞄 %ls",L"  瞄具 %ls / 自瞄 %ls",L"  照準器 %ls / 自動 %ls")
