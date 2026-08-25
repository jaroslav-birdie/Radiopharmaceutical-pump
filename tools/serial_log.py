#!/usr/bin/env python3
"""Zachytavani sericoveho vystupu do souboru + soucasny vypis na obrazovku.

Testovaci sketche v tools/ posilaji data pres Serial Monitor. Ten je ale
neuklada, takze se dlouhy beh nedá vyhodnotit. Tenhle skript ctenim
neblokuje: co prijde, hned zapise do souboru a zaroven vypise, takze je
videt prubeh a zaroven zustane zaznam.

Pouziti:
    tools/serial_log.py /dev/ttyUSB0 beh1.csv
    tools/serial_log.py /dev/ttyUSB0 beh1.csv --baud 9600

Psani do sketche (potvrzeni mezi cykly, prikazy) resi teze konzole:
cokoliv, co napises a odesles Enterem, jde na seriovou linku.

Vyzaduje pyserial:  pip install pyserial
"""

import argparse
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("chybi pyserial - nainstaluj: pip install pyserial")


def reader(port, out, stop):
    """Cte ze seriove linky, zapisuje do souboru i na stdout."""
    while not stop.is_set():
        try:
            raw = port.readline()
        except serial.SerialException as exc:
            print(f"\n[chyba cteni] {exc}", file=sys.stderr)
            stop.set()
            return
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        print(text, flush=True)
        out.write(text + "\n")
        out.flush()          # po kazdem radku - beh trva desitky minut
                             # a nesmi se ztratit, kdyz se to preusi


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="seriovy port, napr. /dev/ttyUSB0 nebo COM3")
    ap.add_argument("soubor", help="kam ulozit log")
    ap.add_argument("--baud", type=int, default=9600, help="prenosova rychlost (9600)")
    args = ap.parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        sys.exit(f"nelze otevrit {args.port}: {exc}")

    # Otevreni portu resetuje Arduino - pockat, nez nabehne setup().
    time.sleep(2)
    port.reset_input_buffer()

    stop = threading.Event()
    with open(args.soubor, "w", encoding="utf-8") as out:
        out.write(f"# zaznam z {args.port} @ {args.baud} Bd, "
                  f"{time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        thread = threading.Thread(target=reader, args=(port, out, stop), daemon=True)
        thread.start()
        print(f"[zaznam do {args.soubor}, Ctrl+C ukonci]", file=sys.stderr)
        try:
            while not stop.is_set():
                cmd = input()
                port.write((cmd + "\n").encode("utf-8"))
        except (KeyboardInterrupt, EOFError):
            pass
        finally:
            stop.set()
            thread.join(timeout=2)
            port.close()
    print("\n[konec]", file=sys.stderr)


if __name__ == "__main__":
    main()
