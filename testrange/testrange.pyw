"""EDF6 测试场启动器：勾选载具、敌人波次和强制装备，一键装进「测试场」任务包（见 gen.RANGE_MISSION）。"""
from __future__ import annotations

import os
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402
import weapons  # noqa: E402

PLAN_FILE = os.path.join(gen.HERE, 'testrange.json')
LOG_FILE = ('Mods', 'Plugins', 'EDF6VehicleCrew.log')
TITLE = 'EDF6 测试场'
PACK = gen.packs.RANGE.name['SC']   # the mission pack's name in the game's 「任务包」 list
KEEP = '（保持存档里的）'


def game_running() -> bool:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/NH'],
                         capture_output=True, text=True, creationflags=0x08000000).stdout
    return 'EDF6.exe' in out


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(TITLE)
        self.plan = gen.load_plan(PLAN_FILE)
        self.game = tk.StringVar(value=os.environ.get('EDF6_DIR', gen.DEFAULT_GAME))
        self.counts: dict[str, tk.IntVar] = {}
        self.npc_counts: dict[str, tk.IntVar] = {}
        self.weapon_error = ''
        try:
            self.weapons = weapons.load(self.game.get())
        except Exception as e:  # the range still works without the loadout part
            self.weapons = []
            self.weapon_error = str(e)
        self._build()
        self._refresh_status()

    # ---------- layout ----------
    def _build(self) -> None:
        pad = {'padx': 6, 'pady': 3}
        top = ttk.Frame(self)
        top.pack(fill='x', **pad)
        ttk.Label(top, text='游戏目录').pack(side='left')
        ttk.Entry(top, textvariable=self.game, width=60).pack(side='left', fill='x', expand=True, padx=4)

        body = ttk.Frame(self)
        body.pack(fill='both', expand=True, **pad)
        self._vehicles(body).pack(side='left', fill='both', expand=True, padx=(0, 6))
        right = ttk.Frame(body)
        right.pack(side='left', fill='both', expand=True)
        self._waves(right).pack(fill='x')
        self._air(right).pack(fill='x', pady=(6, 0))
        self._loadout(right).pack(fill='x', pady=(6, 0))

        bar = ttk.Frame(self)
        bar.pack(fill='x', **pad)
        ttk.Label(bar, text='地图').pack(side='left')
        self.site = ttk.Combobox(bar, values=[x.label for x in gen.SITES], state='readonly', width=20)
        self.site.current(next(i for i, x in enumerate(gen.SITES) if x.source == self.plan.site))
        self.site.pack(side='left', padx=(4, 10))
        ttk.Button(bar, text='安装', command=self.install).pack(side='left')
        ttk.Button(bar, text='卸载', command=self.uninstall).pack(side='left', padx=6)
        ttk.Button(bar, text='打开插件日志', command=self.open_log).pack(side='left')
        self.status = ttk.Label(self, anchor='w', foreground='#555')
        self.status.pack(fill='x', **pad)

    def _vehicles(self, parent: tk.Widget) -> ttk.LabelFrame:
        box = ttk.LabelFrame(parent, text='载具（数量，0 = 不放；地图上最多 12 台）')
        ttk.Label(box, text='空车').grid(row=0, column=0)
        ttk.Label(box, text='NPC 驾驶').grid(row=0, column=1)
        for row, (sgo, label) in enumerate(gen.VEHICLES, start=1):
            var = tk.IntVar(value=self.plan.vehicles.get(sgo, 0))
            npc = tk.IntVar(value=self.plan.friends.get(sgo, 0))
            self.counts[sgo] = var
            self.npc_counts[sgo] = npc
            ttk.Spinbox(box, from_=0, to=4, width=3, textvariable=var).grid(row=row, column=0, padx=4, pady=1)
            ttk.Spinbox(box, from_=0, to=12, width=3, textvariable=npc).grid(row=row, column=1, padx=4, pady=1)
            ttk.Label(box, text=label).grid(row=row, column=2, sticky='w')
        last = len(gen.VEHICLES) + 1
        self.vlevel = tk.DoubleVar(value=self.plan.vehicle_level)
        ttk.Label(box, text='载具等级（1 = 普通，越高越硬）').grid(row=last, column=2, sticky='w', pady=(6, 0))
        ttk.Spinbox(box, from_=0.5, to=5, increment=0.5, width=5, textvariable=self.vlevel).grid(
            row=last, column=0, columnspan=2, pady=(6, 0))
        return box

    def _waves(self, parent: tk.Widget) -> ttk.LabelFrame:
        w = self.plan.waves
        box = ttk.LabelFrame(parent, text='敌人波次（离玩家 180–450 米刷出）')
        self.w_on = tk.BooleanVar(value=w.enabled)
        ttk.Checkbutton(box, text='刷敌人', variable=self.w_on).grid(row=0, column=0, sticky='w')
        ttk.Button(box, text='靶场预设（安装器装的那一版：只有靶子，新加的载具各一台）', command=self.target_range).grid(
            row=0, column=1, sticky='w', padx=6)
        labels = [e[1] for e in gen.ENEMIES]
        self.w_enemy = tk.StringVar(value=next((l for s, l, _ in gen.ENEMIES if s == w.enemy), labels[0]))
        ttk.Label(box, text='敌人种类').grid(row=1, column=0, sticky='w')
        ttk.Combobox(box, values=labels, textvariable=self.w_enemy, state='readonly', width=22).grid(row=1, column=1, sticky='w')
        self.w_vars: dict[str, tk.Variable] = {}
        fields = [('per_wave', '每波数量', tk.IntVar), ('max_alive', '场上少于几只时刷下一波', tk.IntVar),
                  ('first_delay', '开局多少秒后刷第一波', tk.DoubleVar), ('interval', '两波最短间隔（秒）', tk.DoubleVar),
                  ('level', '敌人等级', tk.DoubleVar)]
        for i, (key, text, kind) in enumerate(fields, start=2):
            var = kind(value=getattr(w, key))
            self.w_vars[key] = var
            ttk.Label(box, text=text).grid(row=i, column=0, sticky='w')
            ttk.Entry(box, textvariable=var, width=8).grid(row=i, column=1, sticky='w', pady=1)
        return box

    def _air(self, parent: tk.Widget) -> ttk.LabelFrame:
        a = self.plan.air
        box = ttk.LabelFrame(parent, text='空战：敌机波次（开启后不刷地面敌人和靶子）')
        self.a_on = tk.BooleanVar(value=a.enabled)
        ttk.Checkbutton(box, text='刷敌机', variable=self.a_on).grid(row=0, column=0, sticky='w')
        ttk.Button(box, text='空战预设（玩家战斗机 + 3 架友军 + 8 架敌机，之后成波补充）', command=self.air_battle).grid(
            row=0, column=1, sticky='w', padx=6)
        self.grand = tk.BooleanVar(value=self.plan.scenario == gen.GRAND)
        ttk.Checkbutton(box, text='大混战（需先用 tools/make_bigmap.py 装大地图）', variable=self.grand).grid(row=9, column=0, sticky='w')
        ttk.Button(box, text='大混战预设（母舰、传送舰、敌机 vs 我方飞机；地面步兵坦克 vs 外星地面部队）',
                   command=self.grand_battle).grid(row=9, column=1, sticky='w', padx=6)
        self.a_vars: dict[str, tk.Variable] = {}
        fields = [('per_wave', '每波敌机数', tk.IntVar), ('max_alive', '敌机少于几架时补充', tk.IntVar),
                  ('first_delay', '开局多少秒后开始补充', tk.DoubleVar), ('interval', '两波最短间隔（秒）', tk.DoubleVar)]
        for i, (key, text, kind) in enumerate(fields, start=1):
            var = kind(value=getattr(a, key))
            self.a_vars[key] = var
            ttk.Label(box, text=text).grid(row=i, column=0, sticky='w')
            ttk.Entry(box, textvariable=var, width=8).grid(row=i, column=1, sticky='w', pady=1)
        return box

    def grand_battle(self) -> None:
        plan = gen.grand_battle(gen.Plan())
        for s, var in self.counts.items():
            var.set(plan.vehicles.get(s, 0))
        for s, var in self.npc_counts.items():
            var.set(plan.friends.get(s, 0))
        self.w_on.set(False)
        self.a_on.set(False)
        self.grand.set(True)

    def target_range(self) -> None:
        plan = gen.target_range(gen.Plan())
        for s, var in self.counts.items():
            var.set(plan.vehicles.get(s, 0))
        for s, var in self.npc_counts.items():
            var.set(plan.friends.get(s, 0))
        self.w_on.set(True)
        self.w_enemy.set(next(l for s, l, _ in gen.ENEMIES if s == plan.waves.enemy))
        for k, var in self.w_vars.items():
            var.set(getattr(plan.waves, k))
        self.a_on.set(False)
        self.grand.set(False)

    def air_battle(self) -> None:
        plan = gen.air_battle(gen.Plan())
        for s, var in self.counts.items():
            var.set(plan.vehicles.get(s, 0))
        for s, var in self.npc_counts.items():
            var.set(plan.friends.get(s, 0))
        self.w_on.set(False)
        self.a_on.set(True)
        for k, var in self.a_vars.items():
            var.set(getattr(plan.air, k))

    def _loadout(self, parent: tk.Widget) -> ttk.LabelFrame:
        box = ttk.LabelFrame(parent, text='强制装备（插件在进关时装上，出关后存档不变）')
        if not self.weapons:
            ttk.Label(box, wraplength=340, text=f'读不到武器表：{self.weapon_error}').pack(anchor='w')
            return box
        l = {'enabled': False, 'class': 0, 'slots': [''] * 6, 'stars': -1, 'refill': False, **self.plan.loadout}
        self.l_on = tk.BooleanVar(value=l['enabled'])
        ttk.Checkbutton(box, text='启用（不勾 = 用出击前自己选的装备）', variable=self.l_on).grid(
            row=0, column=0, columnspan=2, sticky='w')
        ttk.Label(box, text='兵种').grid(row=1, column=0, sticky='w')
        self.l_class = ttk.Combobox(box, values=weapons.CLASSES, state='readonly', width=28)
        self.l_class.current(int(l['class']))
        self.l_class.grid(row=1, column=1, sticky='w', pady=1)
        self.l_class.bind('<<ComboboxSelected>>', lambda _e: self._fill_slots([''] * 6))
        self.slot_labels: list[ttk.Label] = []
        self.slot_boxes: list[ttk.Combobox] = []
        for i in range(6):
            lab = ttk.Label(box)
            lab.grid(row=2 + i, column=0, sticky='w')
            cb = ttk.Combobox(box, width=40)
            cb.grid(row=2 + i, column=1, sticky='w', pady=1)
            cb.bind('<KeyRelease>', lambda e, i=i: self._filter(i, e))
            self.slot_labels.append(lab)
            self.slot_boxes.append(cb)
        ttk.Label(box, text='星级（属性等级）').grid(row=8, column=0, sticky='w')
        self.l_stars = ttk.Combobox(box, state='readonly', width=28,
                                    values=['保持存档里的（没拿过的武器是 0 星）'] + [f'全部 {n} 星' for n in range(11)])
        self.l_stars.current(int(l['stars']) + 1)
        self.l_stars.grid(row=8, column=1, sticky='w', pady=1)
        self.l_refill = tk.BooleanVar(value=bool(l['refill']))
        ttk.Checkbutton(box, text='开局补满弹药和空袭兵载具（不用先攒呼叫点数）', variable=self.l_refill).grid(
            row=9, column=0, columnspan=2, sticky='w')
        ttk.Label(box, wraplength=340, foreground='#555', justify='left',
                  text='下拉框里可以直接打字筛选。选「保持存档里的」的格子不改。只对离线 1P 生效，'
                       '而且对所有任务都生效：不用时取消勾选再点安装，或点卸载。').grid(
            row=10, column=0, columnspan=2, sticky='w', pady=(4, 0))
        self._fill_slots(l['slots'])
        return box

    def _slot_count(self) -> int:
        return len(weapons.SLOT_NAMES[self.l_class.current()])

    def _slot_choices(self, i: int) -> list[weapons.Weapon]:
        return weapons.for_slot(self.weapons, self.l_class.current(), i)

    def _fill_slots(self, names: list[str]) -> None:
        cls = self.l_class.current()
        for i, (lab, cb) in enumerate(zip(self.slot_labels, self.slot_boxes)):
            if i >= self._slot_count():
                lab.config(text='')
                cb.set('')
                cb.config(values=[], state='disabled')
                continue
            choices = self._slot_choices(i)
            lab.config(text=weapons.SLOT_NAMES[cls][i])
            cb.config(values=[KEEP] + [w.label() for w in choices], state='normal')
            chosen = next((w for w in choices if i < len(names) and w.sgo == names[i]), None)
            cb.set(chosen.label() if chosen else KEEP)

    def _filter(self, i: int, event: tk.Event) -> None:
        if event.keysym in ('Up', 'Down', 'Return', 'Escape', 'Tab'):
            return
        text = self.slot_boxes[i].get().strip().lower()
        labels = [w.label() for w in self._slot_choices(i)]
        self.slot_boxes[i].config(values=[KEEP] + [x for x in labels if text in x.lower()])

    def _loadout_choice(self) -> dict:
        if not self.weapons:
            return self.plan.loadout
        slots = []
        for i, cb in enumerate(self.slot_boxes):
            if i >= self._slot_count() or cb.get() in ('', KEEP):
                slots.append('')
                continue
            match = next((w for w in self._slot_choices(i) if w.label() == cb.get()), None)
            if match is None:
                name = weapons.SLOT_NAMES[self.l_class.current()][i]
                raise ValueError(f'{name}：「{cb.get()}」不是列表里的武器，请从下拉框里选')
            slots.append(match.sgo)
        return {'enabled': bool(self.l_on.get()), 'class': self.l_class.current(), 'slots': slots,
                'stars': self.l_stars.current() - 1, 'refill': bool(self.l_refill.get())}

    # ---------- actions ----------
    def _collect(self) -> gen.Plan:
        plan = gen.Plan()
        plan.vehicles = {s: int(v.get()) for s, v in self.counts.items() if int(v.get()) > 0}
        plan.friends = {s: int(v.get()) for s, v in self.npc_counts.items() if int(v.get()) > 0}
        plan.vehicle_level = float(self.vlevel.get())
        enemy = next(s for s, l, _ in gen.ENEMIES if l == self.w_enemy.get())
        plan.waves = gen.Waves(enabled=bool(self.w_on.get()), enemy=enemy,
                               **{k: v.get() for k, v in self.w_vars.items()})
        plan.air = gen.AirWaves(enabled=bool(self.a_on.get()), **{k: v.get() for k, v in self.a_vars.items()})
        plan.scenario = gen.GRAND if self.grand.get() else ''
        plan.loadout = self._loadout_choice()
        plan.site = gen.SITES[self.site.current()].source
        return plan

    def install(self) -> None:
        try:
            plan = self._collect()
        except tk.TclError as e:
            messagebox.showerror(TITLE, f'有一项不是数字：{e}')
            return
        except ValueError as e:
            messagebox.showerror(TITLE, str(e))
            return
        gen.save_plan(PLAN_FILE, plan)
        try:
            placed = gen.install(self.game.get(), plan)
            equip = weapons.write_loadout(self.game.get(), plan.loadout)
        except Exception as e:  # shown to the user as-is
            messagebox.showerror(TITLE, str(e))
            self._refresh_status()
            return
        lines = [f'已装进「{PACK}」任务包（离线 / 在线模式的「任务包」里选它，唯一的一关），难度随意。', '']
        lines += placed or ['（没有放载具）']
        lines += ['', '强制装备：'] + equip if equip else ['', '装备：用出击前自己选的']
        if game_running():
            lines += ['', '游戏正在运行：重新进入测试场即生效。']
        messagebox.showinfo(TITLE, '\n'.join(lines))
        self._refresh_status()

    def uninstall(self) -> None:
        try:
            removed = gen.uninstall(self.game.get())
        except Exception as e:  # shown to the user as-is (the game running, a mode table changed by another tool)
            messagebox.showerror(TITLE, str(e))
            self._refresh_status()
            return
        weapons.remove_loadout(self.game.get())   # after the range: a failed uninstall keeps the loadout it shows
        messagebox.showinfo(TITLE, f'已删除测试场关卡和「{PACK}」任务包，强制装备已关闭。' if removed
                            else '没装过测试场；强制装备已关闭。')
        self._refresh_status()

    def open_log(self) -> None:
        path = os.path.join(self.game.get(), *LOG_FILE)
        if os.path.isfile(path):
            os.startfile(path)
        else:
            messagebox.showinfo(TITLE, f'还没有日志：{path}')

    def _refresh_status(self) -> None:
        state = f'已装（「{PACK}」任务包）' if gen.installed(self.game.get()) else '未安装'
        forced = '开' if os.path.isfile(weapons.loadout_path(self.game.get())) else '关'
        self.status.config(text=f'测试场：{state}　强制装备：{forced}　　设置保存在 {PLAN_FILE}')


if __name__ == '__main__':
    App().mainloop()
