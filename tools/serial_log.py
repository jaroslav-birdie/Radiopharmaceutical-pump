#!/usr/bin/env python3
"""Zaloguje výstup z Arduina do souboru (a zároveň ho vypisuje na obrazovku).

Používá se pro diagnostické sketche v tools/ (capacitive_test,
capacitive_sweep_test, capacitive_cycle_test), jejichž výstup je moc
dlouhý na pohodlné kopírování z Arduino Serial Monitoru.

Vyžaduje: pip install pyserial

Použití:
    python tools/serial_log.py <port> [baud] [vystupni_soubor]

Příklady:
    python tools/serial_log.py COM5
    python tools/serial_log.py /dev/ttyACM0 9600 cyklus1.csv
    python tools/serial_log.py /dev/cu.usbmodem14101 9600

Port najdeš:
    - Windows: Správce zařízení -> Porty (COM a LPT), např. "COM5"
    - macOS:   ls /dev/cu.*  (typicky /dev/cu.usbmodemXXXX)
    - Linux:   ls /dev/tty*  (typicky /dev/ttyACM0 nebo /dev/ttyUSB0)

DŮLEŽITÉ: Arduino Serial Monitor musí být PŘED spuštěním zavřený - port
může držet otevřený jen jeden program najednou.

Ukončení: Ctrl+C. Soubor se průběžně flushuje, takže i při přerušení
uprostřed běhu zůstane zachyceno vše až po poslední řádek.
"""
import sys
import time

try:
    import serial
except ImportError:
    print("Chybí knihovna pyserial. Nainstaluj: pip install pyserial")
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 9600
    outfile = sys.argv[3] if len(sys.argv) > 3 else time.strftime("capture_%Y%m%d_%H%M%S.txt")

    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"Nejde otevrit port {port}: {e}")
        print("Je Arduino Serial Monitor zavreny? Je port spravne?")
        sys.exit(1)

    print(f"Poslouchám {port} @ {baud} Bd, ukládám do {outfile}")
    print("Ctrl+C pro ukončení.\n")

    with open(outfile, "w", encoding="utf-8") as f:
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                print(text)
                f.write(text + "\n")
                f.flush()
        except KeyboardInterrupt:
            print(f"\nZastaveno. Uloženo do {outfile}")
        finally:
            ser.close()


if __name__ == "__main__":
    main()
