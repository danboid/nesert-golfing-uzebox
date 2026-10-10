"""Drive cuzebox-ex through its localhost API: scripted SNES pad / SNES mouse input per port, screenshots at frames.

Masks are cuzebox-ex's active-high 32-bit packets (bit n = n-th bit clocked out of the port).
"""
import time, os, sys
# cuzebox-ex's own API client (tools/cuzebox_api.py in its repo)
sys.path.insert(0, os.path.join(os.environ.get('CUZEBOX_EX_DIR', os.path.expanduser('~/src/cuzebox-ex')), 'tools'))
from cuzebox_api import CUzeBoxApi

BTN = dict(B=1<<0, Y=1<<1, SELECT=1<<2, START=1<<3, UP=1<<4, DOWN=1<<5, LEFT=1<<6, RIGHT=1<<7,
           A=1<<8, X=1<<9, L=1<<10, R=1<<11)

def pad(*b):
    m = 0
    for x in b: m |= BTN[x]
    return m

def _rev8(v):
    return int('{:08b}'.format(v)[::-1], 2)

def mouse(dx=0, dy=0, L=False, R=False):
    """SNES mouse report: bits 0-7 zero, bits 8-15 = R,L,sens,sens,0,0,0,1 (bit 8 first), then Y, then X byte, MSB first"""
    m = 0x8000 | (0x200 if L else 0) | (0x100 if R else 0)
    yb = (0x80 if dy < 0 else 0) | min(abs(dy), 127)
    xb = (0x80 if dx < 0 else 0) | min(abs(dx), 127)
    return m | (_rev8(yb) << 16) | (_rev8(xb) << 24)

class Run:
    def __init__(self):
        self.a = CUzeBoxApi(); self.a.connect()
    def cmd(self, s):
        r = self.a.command(s)
        if not r.get('ok'): raise RuntimeError('%s -> %s' % (s, r))
        return r
    def load(self, path):
        self.cmd('LOAD_ROM WAIT ' + os.path.abspath(path))
        self.frame = self.cmd('GET_FRAME').get('frame')
    def run(self, n):
        self.cmd('RUN_FRAMES %d' % n)
        t = time.time()
        while time.time() - t < 600:
            if self.cmd('GET_STATE').get('run_frames_remaining', 0) == 0: return
            time.sleep(0.05)
        raise RuntimeError('RUN_FRAMES timeout')
    def play(self, p0, p1, shots, outdir, tag):
        """p0/p1: lists of (frames, mask) for port 1 / port 2; shots: frame numbers (from start) to screenshot"""
        def expand(seq):
            out = []
            for n, m in seq: out += [m] * n
            return out
        e0, e1 = expand(p0), expand(p1)
        total = max([len(e0), len(e1)] + [s + 1 for s in shots])
        e0 += [e0[-1] if e0 else 0] * (total - len(e0)); e1 += [e1[-1] if e1 else 0] * (total - len(e1))
        # feed per run segment: runs of identical (m0, m1) pairs, split at screenshot frames
        os.makedirs(outdir, exist_ok=True)
        cuts = sorted(set([0, total] + [s for s in shots]))
        f = 0; files = {}
        while f < total:
            nxt = min([c for c in cuts if c > f] + [total])
            g = f
            while g < nxt:
                h = g
                while h < nxt and e0[h] == e0[g] and e1[h] == e1[g]: h += 1
                self.cmd('SET_INPUT 0 %d %d' % (e0[g], h - g))
                self.cmd('SET_INPUT 1 %d %d' % (e1[g], h - g))
                self.run(h - g)
                g = h
            f = nxt
            if f in shots:
                p = os.path.abspath('%s/%s_f%04d.bmp' % (outdir, tag, f))
                self.cmd('SCREENSHOT ' + p); files[f] = p
        return files
