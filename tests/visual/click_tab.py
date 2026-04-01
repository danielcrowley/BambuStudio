#!/usr/bin/env python3
"""
click_tab.py  <window_id>  <tab_name>

Finds a tab in BambuStudio's top tab bar by OCR-ing the tab button strip,
then clicks the centre of the matching label.

Dependencies (installed in Dockerfile.visual-test):
  tesseract-ocr, python3-pil (Pillow), python3-pytesseract, python3-xlib
"""
import sys
import os
import subprocess
import tempfile

try:
    from PIL import Image, ImageOps, ImageFilter
    import pytesseract
    import Xlib.display
except ImportError as e:
    print(f"Missing dependency: {e}", file=sys.stderr)
    sys.exit(2)


def get_window_geometry(window_id: str) -> tuple[int, int, int, int]:
    """Return (x, y, width, height) of the given window in screen coordinates."""
    out = subprocess.check_output(
        ["xdotool", "getwindowgeometry", "--shell", window_id],
        text=True,
    )
    env: dict[str, int] = {}
    for line in out.splitlines():
        k, _, v = line.partition("=")
        env[k.strip()] = int(v.strip())
    return env["X"], env["Y"], env["WIDTH"], env["HEIGHT"]


def screenshot_region(x: int, y: int, w: int, h: int, path: str) -> None:
    """Capture a screen region using scrot."""
    display = os.environ.get("DISPLAY", ":0")
    subprocess.check_call(
        ["scrot", f"--display={display}", f"--geometry={w}x{h}+{x}+{y}", path]
    )


def preprocess_for_ocr(img: Image.Image) -> Image.Image:
    """Sharpen + upscale the strip so tesseract reads small text reliably."""
    img = img.convert("RGB")
    img = img.resize((img.width * 3, img.height * 3), Image.LANCZOS)
    img = ImageOps.autocontrast(img)
    img = img.filter(ImageFilter.SHARPEN)
    return img


def find_tab_center(
    strip_img: Image.Image, tab_name: str
) -> tuple[int, int] | None:
    """
    Run tesseract on the pre-processed image and return the (cx, cy) of the
    first word that matches tab_name (case-insensitive), in original-scale
    coordinates (i.e. divided back by the 3× upscale).
    """
    data = pytesseract.image_to_data(
        strip_img, output_type=pytesseract.Output.DICT, config="--psm 7"
    )
    target = tab_name.lower()
    scale = 3  # must match the upscale factor above

    for i, text in enumerate(data["text"]):
        if target in text.lower() and int(data["conf"][i]) > 30:
            cx = (data["left"][i] + data["width"][i] // 2) // scale
            cy = (data["top"][i] + data["height"][i] // 2) // scale
            return cx, cy

    return None


def main() -> int:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <window_id> <tab_name>", file=sys.stderr)
        return 1

    window_id = sys.argv[1]
    tab_name = sys.argv[2]

    win_x, win_y, win_w, win_h = get_window_geometry(window_id)
    print(f"Window geometry: {win_w}x{win_h} at ({win_x},{win_y})")

    # The BBLTopbar (custom title bar) is ~30 px; below it the Notebook tab
    # buttons live in a strip ~40 px tall.  Capture both rows to be safe.
    strip_h = 80
    strip_y = win_y  # start from very top of window client area

    tmp = tempfile.mktemp(suffix=".png")
    try:
        screenshot_region(win_x, strip_y, win_w, strip_h, tmp)
        raw = Image.open(tmp)
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass

    processed = preprocess_for_ocr(raw)

    result = find_tab_center(processed, tab_name)
    if result is None:
        print(f"Tab '{tab_name}' not found via OCR.", file=sys.stderr)
        # Dump OCR output for diagnosis
        text = pytesseract.image_to_string(processed, config="--psm 6")
        print(f"OCR saw: {text!r}", file=sys.stderr)
        return 1

    rel_x, rel_y = result
    abs_x = win_x + rel_x
    abs_y = strip_y + rel_y
    print(f"Found '{tab_name}' at window-relative ({rel_x},{rel_y}), screen ({abs_x},{abs_y})")

    subprocess.check_call(["xdotool", "mousemove", str(abs_x), str(abs_y)])
    subprocess.check_call(["xdotool", "click", "1"])
    print(f"Clicked '{tab_name}' tab.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
