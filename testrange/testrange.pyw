"""EDF6 测试场启动器：勾选载具与敌人波次，一键装进第 1 关。"""
from __future__ import annotations

import os
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402

PLAN_FILE = os.path.join(gen.HERE, 'testrange.json')
LOG_FILE = ('Mods', 'Plugins', 'EDF6VehicleCrew.log')


def game_running() -> bool:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/NH'],
                         capture_output=True, text=True, creationflags=0x08000000).stdout
    return 'EDF6.exe' in out


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title('EDF6 测试场')
        self.plan = gen.load_plan(PLAN_FILE)
        self.game = tk.StringVar(value=os.environ.get('EDF6_DIR', gen.DEFAULT_GAME))
        self.counts: dict[str, tk.IntVar] = {}
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
        self._loadout(right).pack(fill='x', pady=(6, 0))

        bar = ttk.Frame(self)
        bar.pack(fill='x', **pad)
        ttk.Button(bar, text='安装到第 1 关', command=self.install).pack(side='left')
        ttk.Button(bar, text='卸载（恢复第 1 关）', command=self.uninstall).pack(side='left', padx=6)
        ttk.Button(bar, text='打开插件日志', command=self.open_log).pack(side='left')
        self.status = ttk.Label(self, anchor='w', foreground='#555')
        self.status.pack(fill='x', **pad)

    def _vehicles(self, parent: tk.Widget) -> ttk.LabelFrame:
        box = ttk.LabelFrame(parent, text='载具（数量，0 = 不放；地图上最多 12 台）')
        for row, (sgo, label) in enumerate(gen.VEHICLES):
            var = tk.IntVar(value=self.plan.vehicles.get(sgo, 0))
            self.counts[sgo] = var
            ttk.Spinbox(box, from_=0, to=4, width=3, textvariable=var).grid(row=row, column=0, padx=4, pady=1)
            ttk.Label(box, text=label).grid(row=row, column=1, sticky='w')
        self.vlevel = tk.DoubleVar(value=self.plan.vehicle_level)
        ttk.Label(box, text='载具等级（1 = 普通，越高越硬）').grid(row=len(gen.VEHICLES), column=1, sticky='w', pady=(6, 0))
        ttk.Spinbox(box, from_=0.5, to=5, increment=0.5, width=5, textvariable=self.vlevel).grid(
            row=len(gen.VEHICLES), column=0, pady=(6, 0))
        return box

    def _waves(self, parent: tk.Widget) -> ttk.LabelFrame:
        w = self.plan.waves
        box = ttk.LabelFrame(parent, text='敌人波次（离玩家 180–450 米刷出）')
        self.w_on = tk.BooleanVar(value=w.enabled)
        ttk.Checkbutton(box, text='刷敌人', variable=self.w_on).grid(row=0, column=0, columnspan=2, sticky='w')
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

    def _loadout(self, parent: tk.Widget) -> ttk.LabelFrame:
        box = ttk.LabelFrame(parent, text='装备')
        ttk.Label(box, wraplength=320, justify='left',
                  text='进关时用的是你在出击前选的兵种和武器（游戏原版行为）。').pack(anchor='w', padx=4, pady=4)
        return box

    # ---------- actions ----------
    def _collect(self) -> gen.Plan:
        plan = gen.Plan()
        plan.vehicles = {s: int(v.get()) for s, v in self.counts.items() if int(v.get()) > 0}
        plan.vehicle_level = float(self.vlevel.get())
        enemy = next(s for s, l, _ in gen.ENEMIES if l == self.w_enemy.get())
        plan.waves = gen.Waves(enabled=bool(self.w_on.get()), enemy=enemy,
                               **{k: v.get() for k, v in self.w_vars.items()})
        plan.loadout = self.plan.loadout
        return plan

    def install(self) -> None:
        try:
            plan = self._collect()
        except (tk.TclError, ValueError) as e:
            messagebox.showerror('EDF6 测试场', f'有一项不是数字：{e}')
            return
        gen.save_plan(PLAN_FILE, plan)
        try:
            placed = gen.install(self.game.get(), plan)
        except Exception as e:  # shown to the user as-is
            messagebox.showerror('EDF6 测试场', str(e))
            return
        note = '\n\n游戏正在运行：重新进入第 1 关即生效。' if game_running() else ''
        messagebox.showinfo('EDF6 测试场', '已装到第 1 关（离线 → 第 1 关，难度随意）。\n\n'
                            + ('\n'.join(placed) or '（没有放载具）') + note)
        self._refresh_status()

    def uninstall(self) -> None:
        msg = '已删除，第 1 关恢复原样。' if gen.uninstall(self.game.get()) else '没装过测试场，什么都没动。'
        messagebox.showinfo('EDF6 测试场', msg)
        self._refresh_status()

    def open_log(self) -> None:
        path = os.path.join(self.game.get(), *LOG_FILE)
        if os.path.isfile(path):
            os.startfile(path)
        else:
            messagebox.showinfo('EDF6 测试场', f'还没有日志：{path}')

    def _refresh_status(self) -> None:
        state = '已安装' if gen.installed(self.game.get()) else '未安装'
        self.status.config(text=f'测试场：{state}　　设置保存在 {PLAN_FILE}')


if __name__ == '__main__':
    App().mainloop()
