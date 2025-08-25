import os, platform, subprocess, time, webbrowser
import serial
import keyboard

try:
    import screen_brightness_control as sbc
except Exception:
    sbc = None

PORT = 'COM7'
BAUD = 115200

ZEN_PATH = r"C:\Program Files\Zen Browser\zen.exe"
BRIGHT_STEP = 10
OS = platform.system()

def open_with_zen(url=None):
    if ZEN_PATH and os.path.exists(ZEN_PATH):
        try:
            subprocess.Popen([ZEN_PATH] + ([url] if url else []))
            return
        except Exception as e:
            print("Failed to start Zen:", e)
    if url: webbrowser.open_new_tab(url)
    else:   webbrowser.open_new_tab("about:blank")

def vol_mute():  keyboard.press_and_release('volume mute')
def vol_up():    keyboard.press_and_release('volume up')
def vol_down():  keyboard.press_and_release('volume down')

def bright_change(delta):
    if OS == "Windows" and sbc:
        try:
            cur = int(round(sbc.get_brightness(display=0)[0]))
            sbc.set_brightness(max(0, min(100, cur + delta)))
        except Exception as e:
            print("Brightness error:", e)

def press_combo(spec):
    # e.g. "ctrl+shift+s" or "win+print_screen"
    keyboard.press_and_release(spec)

def send_text(s):
    keyboard.write(s)

def handle(raw):
    # raw could be "OPEN_URL" or "OPEN_URL|https://..."
    if '|' in raw:
        cmd, arg = raw.split('|', 1)
    else:
        cmd, arg = raw, ""
    cmd = cmd.strip().upper()

    print("CMD:", cmd, "ARG:", arg)

    if cmd == "ZEN":
        open_with_zen()
    elif cmd == "OPEN_URL":
        open_with_zen(arg or "about:blank")
    elif cmd == "VOL_UP":
        vol_up()
    elif cmd == "VOL_DOWN":
        vol_down()
    elif cmd == "VOL_MUTE":
        vol_mute()
    elif cmd == "BRIGHT_UP":
        bright_change(+BRIGHT_STEP)
    elif cmd == "BRIGHT_DOWN":
        bright_change(-BRIGHT_STEP)
    elif cmd == "KEY_COMBO":
        if arg: press_combo(arg)
    elif cmd == "SEND_TEXT":
        if arg: send_text(arg)
    elif cmd in ("PAGE_NEXT","PAGE_PREV","NONE"):
        # handled on Arduino side for UI; nothing to do here
        pass
    else:
        print("Unknown command:", cmd)

def main():
    while True:
        try:
            with serial.Serial(PORT, BAUD, timeout=1) as ser:
                print("Connected to", PORT)
                while True:
                    line = ser.readline().decode(errors='ignore').strip()
                    if line.startswith("BTN:"):
                        handle(line[4:].strip())
        except serial.SerialException as e:
            print("Serial error:", e)
            time.sleep(2)

if __name__ == "__main__":
    main()
