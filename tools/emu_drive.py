#!/usr/bin/env python3
"""Drives pcsx-abnxt through its debug driver (frontend/ab/ab_debug.c) - keys in, frames out, over a
socket - the way tools/ab_drive.py drives the launcher. A whole walk through the menus takes seconds and
needs nobody at the machine; a crash is caught as the connection dying, with the log's tail.

  python tools/emu_drive.py start [--game CUE | $AB_GAME] [--port N] [--emu-args "..."] [--no-bios] [--fullscreen]
                                 [--exe PATH]          another build than build_win's, e.g. build_win_rel's
                                                       before a release is published
                                       the Windows build (build_win/pcsx-ab.exe) on a game, with
                                       AB_DEBUG_PORT; waits for the first frame. The BIOS files
                                       (romw.bin, romJP.bin) from $AB_BIOS_DIR, else <repo>/bios, are copied
                                       into build_win/run/.pcsx/bios unless --no-bios (the PCSX menu needs a
                                       real BIOS: with HLE there is no menu to walk); none found = an error
                                       naming AB_BIOS_DIR. The repo holds no BIOS: keep your own in bios/
                                       (git-ignored) or point AB_BIOS_DIR at them
  python tools/emu_drive.py stop
  python tools/emu_drive.py run "<script>"    commands separated by ';'
  python tools/emu_drive.py <command> ...     one command, e.g. `shot a.png`, `press down`, `status`
  python tools/emu_drive.py sheet OUT.png IN1.png ...   a contact sheet of shots (Pillow)

The script language is the driver's - press <key> [ms], down/up <key>, wait <ms>, shot <file>, frames,
screen, row, status, quit - with four of the client's own: `wait_screen <name> [timeout s]` polls
`screen` until that screen shows (boot, game, menu, pcsx, disc, message), `select <text>` presses Down
until the highlighted row's name contains the text (`row` is what the emulator answers), `enter <text>`
is that and Cross, and `rows <n>` is n presses of Down. Key names are in_sdl2's, lower case: escape
return up down left right z x s d c v f1..f12, and the console's front buttons eject (Open) and reset.
A `shot` waits for the next frame to be presented, so "press down; shot a.png" shows the result of the
press; .png paths are converted from the driver's BMP with Pillow.

  python tools/emu_drive.py start
  python tools/emu_drive.py run "press escape; wait_screen menu; enter PCSX menu; wait_screen pcsx;
                                 enter Options; enter [Display]; shot display.png"

For a Pi or the console, start the emulator there with AB_DEBUG_PORT=<port> and forward it
(ssh -L 7799:127.0.0.1:7799 ...), then use --host/--port here; the driver only ever listens on the
loopback.
"""
import os
import socket
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
RUN_DIR = os.path.join(REPO, 'build_win', 'run')
EXE = os.path.join(REPO, 'build_win', 'pcsx-ab.exe')
BIOS_ENV = 'AB_BIOS_DIR'
BIOS_FILES = ('romw.bin', 'romJP.bin')
GAME_ENV = 'AB_GAME'
DEFAULT_PORT = 7799


def pid_file(port):
    return os.path.join(RUN_DIR, f'emu_drive-{port}.pid')


class EmulatorGone(RuntimeError):
    pass


class Driver:
    def __init__(self, port=DEFAULT_PORT, host='127.0.0.1', quiet=False):
        try:
            self.sock = socket.create_connection((host, port), timeout=30)
        except OSError as e:
            if quiet:       # start() polls until the driver is up: not an error yet
                raise
            raise EmulatorGone(f'no driver on {host}:{port} ({e}) - is the emulator running'
                               ' (tools/emu_drive.py start)?' + log_tail())
        self.buf = b''

    def cmd(self, line):
        # a crash is the connection dying (or, on Windows, the process frozen in the crash reporter and
        # answering nothing): either way the command is where it died, and the log says why
        try:
            self.sock.sendall((line + '\n').encode('utf-8'))
            while b'\n' not in self.buf:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise EmulatorGone(f'{line!r}: the emulator closed the connection' + log_tail())
                self.buf += chunk
        except socket.timeout:
            raise EmulatorGone(f'{line!r}: no answer in {self.sock.gettimeout():.0f}s'
                               ' - the emulator is wedged or crashed' + log_tail())
        except OSError as e:
            raise EmulatorGone(f'{line!r}: {e}' + log_tail())
        reply, self.buf = self.buf.split(b'\n', 1)
        reply = reply.decode('utf-8', 'replace')
        if reply.startswith('err'):
            raise RuntimeError(f'{line!r}: {reply}')
        return reply

    def wait_screen(self, name, timeout=15.0):
        end = time.time() + timeout
        current = ''
        while time.time() < end:
            current = self.cmd('screen').split(' ', 1)[-1]
            if current == name:
                return 'ok ' + name
            time.sleep(0.05)
        raise RuntimeError(f'screen {name} did not show (now: {current})')

    def select(self, text, limit=40):
        """the cursor onto the row whose name contains `text` (the emulator keeps the highlighted row's
        name: libpicofe's menu_sel_name), so a script names rows instead of counting keypresses"""
        seen = []
        for _ in range(limit):
            row = self.cmd('row').split(' ', 1)[-1]
            if text.lower() in row.lower():
                return 'ok ' + row
            seen.append(row)
            self.cmd('press down')
            time.sleep(0.09)    # the menu redraws every 70 ms; the row is what the last draw saw
        raise RuntimeError(f'no row matching {text!r} (rows seen: {", ".join(dict.fromkeys(seen))})')

    def shot(self, path):
        path = os.path.abspath(path)
        png = path.lower().endswith('.png')
        bmp = path[:-4] + '.bmp' if png else path
        os.makedirs(os.path.dirname(bmp), exist_ok=True)
        reply = self.cmd('shot ' + bmp)
        if png:
            from PIL import Image
            Image.open(bmp).convert('RGB').save(path)
            os.remove(bmp)
            reply = 'ok ' + path
        return reply

    def run(self, script):
        out = []
        for part in script.replace('\n', ' ').split(';'):
            part = part.strip()
            if not part:
                continue
            words = part.split()
            if words[0] == 'shot':
                out.append(self.shot(part.split(None, 1)[1].strip()))
            elif words[0] == 'wait_screen':
                out.append(self.wait_screen(words[1], float(words[2]) if len(words) > 2 else 15.0))
            elif words[0] == 'select':
                out.append(self.select(part.split(None, 1)[1].strip()))
            elif words[0] == 'enter':   # select a row and press Cross
                out.append(self.select(part.split(None, 1)[1].strip()))
                out.append(self.cmd('press return'))
                time.sleep(0.2)
            elif words[0] == 'rows':
                for _ in range(int(words[1])):
                    self.cmd('press down')
                out.append('ok')
            else:
                out.append(self.cmd(part))
        return out

    def close(self):
        self.sock.close()


def log_tail(lines=12):
    """what the emulator said last - a crash leaves its reason in build_win/run/err.txt"""
    out = []
    for name in ('err.txt', 'out.txt'):
        path = os.path.join(RUN_DIR, name)
        if not os.path.exists(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            tail = f.read().splitlines()[-lines:]
        if tail:
            out.append('\n--- ' + name + ' ---\n' + '\n'.join(tail))
    return ''.join(out)


def bios_dir():
    """where the BIOS files come from: $AB_BIOS_DIR, else <repo>/bios (no machine path is written here)"""
    return os.environ.get(BIOS_ENV) or os.path.join(REPO, 'bios')


def link_bios():
    """a real BIOS in the run directory: without one the emulator boots HLE, where a save state cannot be
    resumed and the PCSX menu has nothing to show. Fails, naming AB_BIOS_DIR, when there is none"""
    dst = os.path.join(RUN_DIR, '.pcsx', 'bios')
    src_dir = bios_dir()
    have = [n for n in BIOS_FILES if os.path.exists(os.path.join(dst, n))]
    found = [n for n in BIOS_FILES if os.path.exists(os.path.join(src_dir, n))]
    if not have and not found:
        sys.exit(f'emu_drive: no BIOS ({" / ".join(BIOS_FILES)}) in {src_dir} - set {BIOS_ENV} to a folder that '
                 f'has them, or pass --no-bios (HLE: no PCSX menu to walk)')
    os.makedirs(dst, exist_ok=True)
    for name in found:
        if not os.path.exists(os.path.join(dst, name)):
            with open(os.path.join(src_dir, name), 'rb') as fi, open(os.path.join(dst, name), 'wb') as fo:
                fo.write(fi.read())
    cfg = os.path.join(RUN_DIR, '.pcsx', 'pcsx.cfg')
    text = open(cfg, encoding='utf-8').read() if os.path.exists(cfg) else ''
    if 'Bios' not in text:
        with open(cfg, 'a', encoding='utf-8') as f:
            f.write('Bios = SET_BY_PCSX\n')   # ab_config.c: romw.bin / romJP.bin by the disc's region


def start(game, port, emu_args, bios=True, fullscreen=False, exe=EXE):
    os.makedirs(RUN_DIR, exist_ok=True)
    if bios:
        link_bios()
    env = dict(os.environ)
    env['AB_DEBUG_PORT'] = str(port)
    env['AB_NO_AUTOSAVE'] = '1'     # no save-state ring in a test run
    argv = [exe, '-cdfile', game] + emu_args
    if fullscreen:
        argv.append('-fullscreen')
    out = open(os.path.join(RUN_DIR, 'out.txt'), 'w')
    err = open(os.path.join(RUN_DIR, 'err.txt'), 'w')
    proc = subprocess.Popen(argv, cwd=RUN_DIR, env=env, stdout=out, stderr=err)
    with open(pid_file(port), 'w') as f:
        f.write(str(proc.pid))
    for _ in range(300):
        time.sleep(0.1)
        try:
            d = Driver(port, quiet=True)
            break
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError(f'the emulator exited with {proc.returncode}' + log_tail())
    else:
        raise RuntimeError('the driver did not answer' + log_tail())
    # the emulation starts before the video does (the BIOS boot presents a frame every now and then):
    # wait for the game's frames to flow, or a shot right after `start` finds nothing drawn yet
    d.wait_screen('game', 60)
    frames = 0
    for _ in range(300):
        frames = int(d.cmd('frames').split()[-1])
        if frames > 30:
            break
        time.sleep(0.1)
    print('started pid %d on port %d: %s' % (proc.pid, port, d.cmd('status')))
    d.close()


def stop(port):
    try:
        d = Driver(port, quiet=True)
        d.cmd('quit')
        d.close()
        time.sleep(1.0)
    except OSError:
        pass
    if os.path.exists(pid_file(port)):
        pid = int(open(pid_file(port)).read().strip())
        subprocess.call(['taskkill', '/PID', str(pid), '/F'],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.remove(pid_file(port))
    print('stopped')


def sheet(out, paths, columns=2, width=640):
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert('RGB') for p in paths]
    w, h = width, width * 9 // 16
    rows = (len(ims) + columns - 1) // columns
    img = Image.new('RGB', (columns * (w + 10), rows * (h + 22)), (30, 30, 30))
    d = ImageDraw.Draw(img)
    for i, (p, im) in enumerate(zip(paths, ims)):
        x, y = (i % columns) * (w + 10), (i // columns) * (h + 22)
        d.text((x + 4, y + 3), os.path.basename(p), fill=(255, 255, 0))
        img.paste(im.resize((w, h)), (x, y + 18))
    img.save(out)
    print(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    port, host = DEFAULT_PORT, '127.0.0.1'
    for name, cast in (('--port', int), ('--host', str)):
        if name in args:
            i = args.index(name)
            value = cast(args[i + 1])
            del args[i:i + 2]
            if name == '--port':
                port = value
            else:
                host = value
    if cmd == 'start':
        game = os.environ.get(GAME_ENV, '')
        if '--game' in args:
            game = args[args.index('--game') + 1]
        if not game:
            sys.exit(f'emu_drive: no game - pass --game CUE or set {GAME_ENV}')
        emu_args = args[args.index('--emu-args') + 1].split() if '--emu-args' in args else []
        exe = args[args.index('--exe') + 1] if '--exe' in args else EXE
        start(game, port, emu_args, '--no-bios' not in args, '--fullscreen' in args, exe)
        return 0
    if cmd == 'stop':
        stop(port)
        return 0
    if cmd == 'sheet':
        sheet(args[0], args[1:])
        return 0
    d = Driver(port, host)
    try:
        for reply in d.run(' '.join(args) if cmd == 'run' else cmd + ' ' + ' '.join(args)):
            print(reply)
    finally:
        d.close()
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except (EmulatorGone, RuntimeError) as exc:   # a crash or a script that lost its way: no traceback
        print(exc)
        sys.exit(1)
