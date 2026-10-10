"""NESert input tests in cuzebox-ex (pad / SNES mouse in either port).
usage: start.sh, then test_input.py ../../default/NESert.hex ../../default/NESert.elf <outdir for screenshots>"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czrun import Run, pad, mouse
ROM, ELF, OUT = [os.path.abspath(a) for a in sys.argv[1:4]]
GAME, TITLE = 0, 1          # SCENE_GAME / SCENE_TITLE in src/nesert.h
r = Run()
def fresh():
    r.load(ROM); r.cmd('LOAD_SYMBOLS ' + ELF)
def mem(a, n):
    d = r.cmd('READ_MEM SRAM %d %d' % (a, n))
    b = d['values']
    if isinstance(b, str): b = bytes.fromhex(b.replace(' ', ''))
    return list(b)
def sym(n, i=0): return mem(r.cmd('READ_SYMBOL ' + n)['addr'] + i, 1)[0]
def u16(n):
    b = mem(r.cmd('READ_SYMBOL ' + n)['addr'], 2); return b[0] | (b[1] << 8)
def addr(n):
    a = r.cmd('READ_SYMBOL ' + n)
    return a['addr'] * 2 if a.get('kind') == 'prog' else a['addr']   # flash symbols are reported as word addresses
results = []
def check(name, cond, info=''):
    results.append((name, bool(cond), info)); print(('PASS ' if cond else 'FAIL ') + name, info)
IDLE_M = mouse()

# T1: mouse in port 2, left click on the logo page starts a 1-player game; splash shows the mouse prompt
fresh(); r.play([(1, 0)], [(150, IDLE_M)], shots=[149], outdir=OUT, tag='t1')
check('splash shows "or click" with a mouse in port 2', u16('scene_text_a') == addr('splash_text_mouse'))
check('mouse in port 2 detected (port 1 not)', sym('mouse_hold', 1) > 0 and sym('mouse_hold', 0) == 0)
r.play([(1, 0)], [(4, mouse(L=True)), (120, IDLE_M)], shots=[123], outdir=OUT, tag='t1b')
check('LMB (port 2) on logo page starts game', sym('scene_mode') == GAME and sym('players') == 1, 'scene_mode=%d' % sym('scene_mode'))

# T2: pad START in port 1 starts the game directly (no mouse)
fresh(); r.play([(150, 0), (4, pad('START')), (120, 0)], [(1, 0)], shots=[], outdir=OUT, tag='t2')
check('pad START (port 1) on logo page starts game', sym('scene_mode') == GAME)
# T3: pad A opens the menu page; RMB opens the menu page
fresh(); r.play([(150, 0), (4, pad('A')), (60, 0)], [(1, 0)], shots=[], outdir=OUT, tag='t3')
check('pad A on logo page opens the menu', sym('scene_mode') == TITLE and sym('scene_logo') == 0)
fresh(); r.play([(1, 0)], [(150, IDLE_M), (4, mouse(R=True)), (60, IDLE_M)], shots=[], outdir=OUT, tag='t3b')
check('RMB on logo page opens the menu', sym('scene_mode') == TITLE and sym('scene_logo') == 0)
# T4: mouse in port 1 works too
fresh(); r.play([(150, IDLE_M), (4, mouse(L=True)), (120, IDLE_M)], [(1, 0)], shots=[], outdir=OUT, tag='t4')
check('LMB with the mouse in port 1 starts game', sym('scene_mode') == GAME)
# T6: corrupted reports never become pad presses
fresh(); r.play([(1, 0)], [(300, 0x8000 | pad('START'))], shots=[], outdir=OUT, tag='t6')
check('invalid mouse report (START bit set in byte 1) ignored', sym('scene_mode') == TITLE and sym('scene_logo') == 1)
fresh(); r.play([(1, 0)], [(150, IDLE_M), (1, pad('START')), (40, IDLE_M), (1, pad('START')), (60, IDLE_M)], shots=[], outdir=OUT, tag='t6b')
check('one-frame pad-like read from the mouse port ignored', sym('scene_mode') == TITLE and sym('scene_logo') == 1)

# T5: the same swing by pad and by mouse gives the same pictures and ball (game started with pad START in both runs,
# mouse plugged into port 2 in both). Screenshots are compared without the emulator's status line; both runs use the
# same file name so its on-screen "screenshot saved" message is the same too.
from PIL import Image
def pic(path): return Image.open(path).convert('RGB').crop((0, 20, 620, 244)).tobytes()
frames = list(range(560, 1000, 10))
def swing_run(p0, p1):
    fresh(); f = r.play(p0, p1, shots=frames, outdir=OUT, tag='swing')
    return [pic(f[g]) for g in frames], [mem(r.cmd('READ_SYMBOL balls_x')['addr'], 4), mem(r.cmd('READ_SYMBOL strokes')['addr'], 1)]
A, sa = swing_run([(150, 0), (4, pad('START')), (406, 0), (45, pad('A', 'LEFT', 'DOWN')), (30, pad('A', 'LEFT')), (400, 0)],
                  [(1000, IDLE_M)])
B, sb = swing_run([(150, 0), (4, pad('START')), (846, 0)],
                  [(560, IDLE_M), (45, mouse(dx=-4, dy=4, L=True)), (30, mouse(dx=-4, L=True)), (400, IDLE_M)])
same = sum(x == y for x, y in zip(A, B))
check('mouse drag swing == pad swing (%d screenshots)' % len(frames), same == len(frames) and sa == sb, '%d/%d identical, ball %s / %s' % (same, len(frames), sa, sb))
check('ball moved and a stroke was counted', A[0] != A[-1] and sa[1][0] == 1)
print('%d/%d passed' % (sum(p for _, p, _ in results), len(results)))
