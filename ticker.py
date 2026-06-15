import time

import requests
import serial

# Configured to your exact port from the screenshot
arduino_port = "COM4"
baud_rate = 9600

# ticker.py pulls the banner from the running dashboard server, so the LCD always
# matches what you see on screen — live data, chosen display mode, and day playback.
PREVIEW_URL = "http://localhost:5000/api/arduino-preview"
LIVE_POLL = 2.0       # live mode: banner rarely changes, no need to poll hard
PLAYBACK_POLL = 2.0   # playback updates once per minute, so a relaxed poll is plenty


def get_banner():
    try:
        resp = requests.get(PREVIEW_URL, timeout=5)
        data = resp.json()
        return data.get("bottom_row"), data.get("is_playback", False)
    except Exception as e:
        print(f"Could not reach dashboard server: {e}")
        return None, False


try:
    print("Opening connection to Arduino...")
    ser = serial.Serial(arduino_port, baud_rate, timeout=1)
    # 4 second delay ensures the Arduino finishes rebooting before we send data
    time.sleep(4)
    print("Connected to Arduino successfully!")
except Exception as e:
    print(f"Error connecting to {arduino_port}: {e}")
    print("Make sure your Arduino IDE Serial Monitor is closed!")
    exit()

print("Streaming dashboard banner to LCD. Press Ctrl+C to stop.")
last_msg = None

while True:
    banner, is_playback = get_banner()

    # During playback we prefix a '~' marker so the Arduino freezes the company + number
    # in place (no scrolling). In live mode there's no marker, so the long banner scrolls.
    if banner:
        msg = ("~" + banner) if is_playback else banner

        # Only push to the LCD when the text actually changed. In live mode this lets the
        # Arduino keep scrolling smoothly; during playback the value changes each frame.
        if msg != last_msg:
            ser.write((msg + "\n").encode("utf-8"))
            last_msg = msg
            print(f"Sent to LCD: {msg}")

    time.sleep(PLAYBACK_POLL if is_playback else LIVE_POLL)
