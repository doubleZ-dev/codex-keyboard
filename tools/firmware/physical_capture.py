"""Local foreground text fixture: records only events delivered to its own window."""
import argparse
import json
from pathlib import Path
import time
import tkinter as tk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='Physical test: U1 macro / U2 gestures / U3 Smart text.')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    root = tk.Tk()
    root.title('Codex Micro physical regression - local test only')
    root.geometry('860x360+100+100')
    tk.Label(root, text=args.label+'\nKeep this text window focused.', font=('Segoe UI', 15)).pack()
    text = tk.Text(root, font=('Consolas', 18))
    text.pack(fill='both', expand=True)
    events = []
    started = time.monotonic()

    def save():
        report = dict(scope='Foreground test window only; no global keyboard hook',
                      elapsed_s=round(time.monotonic()-started, 3),
                      text=text.get('1.0', 'end-1c'), events=events)
        (args.out/'physical-capture.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def key(event):
        events.append(dict(time_s=round(time.monotonic()-started, 3),
                           kind=str(event.type), key=event.keysym,
                           keycode=event.keycode, char=event.char))
        root.after_idle(save)

    text.bind('<KeyPress>', key)
    text.bind('<KeyRelease>', key)
    def tick():
        save()
        root.after(1000, tick)
    def close():
        save()
        root.destroy()
    root.protocol('WM_DELETE_WINDOW', close)
    root.after(200, lambda: (root.lift(), root.focus_force(), text.focus_set()))
    tick()
    root.mainloop()


if __name__ == '__main__':
    main()
