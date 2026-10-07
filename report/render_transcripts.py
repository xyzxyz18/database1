#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 report/transcripts/*.txt 渲染成终端风格的 PNG“截图”，输出到 report/screenshots/。

使用的等宽中文字体（Noto Sans Mono CJK SC）中，一个汉字正好等于两个英文字符宽度，
与程序内部按显示宽度对齐的做法一致，所以表格能够严格对齐。

运行：python3 report/render_transcripts.py
"""

import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRANS_DIR = os.path.join(ROOT, "report", "transcripts")
SHOT_DIR = os.path.join(ROOT, "report", "screenshots")

FONT_PATH = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FONT_INDEX = 7          # Noto Sans Mono CJK SC
FONT_SIZE = 17
LINE_H = 25
PAD = 16
TITLE_H = 34
MAX_LINES = 46          # 一张图最多多少行，超出则分页
MAX_RATIO = 1.70        # 一张图的最大高宽比（太高在报告里会占满整页）

BG = (16, 20, 24)
FG = (216, 222, 233)
TITLE_BG = (33, 43, 54)
TITLE_FG = (136, 192, 208)
BORDER = (60, 70, 80)
SHADOW = (0, 0, 0)


def load_font(size=FONT_SIZE):
    return ImageFont.truetype(FONT_PATH, size, index=FONT_INDEX)


def split_lines(lines, font):
    """按“不超过 MAX_LINES 行、高宽比不超过 MAX_RATIO”把长会话切成若干页，并尽量分得均匀。"""
    full_w = max((font.getlength(ln) for ln in lines), default=100) + 2 * PAD
    by_ratio = int((MAX_RATIO * full_w - TITLE_H - 2 * PAD) / LINE_H)
    max_lines = max(8, min(MAX_LINES, by_ratio))
    if len(lines) <= max_lines:
        return [lines]
    pages = -(-len(lines) // max_lines)          # 向上取整
    size = -(-len(lines) // pages)               # 尽量均匀
    return [lines[i:i + size] for i in range(0, len(lines), size)]


def render(lines, title, out_path, font):
    text_w = max((font.getlength(ln) for ln in lines), default=100)
    width = int(text_w) + 2 * PAD
    height = TITLE_H + len(lines) * LINE_H + 2 * PAD

    img = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(img)

    # 标题栏
    d.rectangle([0, 0, width, TITLE_H], fill=TITLE_BG)
    d.text((PAD, (TITLE_H - FONT_SIZE) // 2 - 3), title,
           font=font, fill=TITLE_FG)
    d.line([0, TITLE_H, width, TITLE_H], fill=BORDER)

    y = TITLE_H + PAD
    for ln in lines:
        d.text((PAD, y), ln, font=font, fill=FG)
        y += LINE_H

    d.rectangle([0, 0, width - 1, height - 1], outline=BORDER)
    img.save(out_path)
    return width, height


def main():
    os.makedirs(SHOT_DIR, exist_ok=True)
    # 清理上一次渲染留下的旧图，避免残留
    for old in os.listdir(SHOT_DIR):
        if old.endswith(".png"):
            os.remove(os.path.join(SHOT_DIR, old))
    font = load_font()

    with open(os.path.join(TRANS_DIR, "meta.json"), encoding="utf-8") as f:
        meta = json.load(f)

    index = []
    for item in meta:
        path = os.path.join(TRANS_DIR, item["file"])
        with open(path, encoding="utf-8") as f:
            text = f.read()
        lines = text.rstrip("\n").split("\n")
        chunks = split_lines(lines, font)
        base = os.path.splitext(item["file"])[0]

        shots = []
        for i, chunk in enumerate(chunks, 1):
            title = "$ ./inventory"
            if len(chunks) > 1:
                title += "    (%d/%d)" % (i, len(chunks))
            name = "%s.png" % base if len(chunks) == 1 else "%s_%d.png" % (base, i)
            out = os.path.join(SHOT_DIR, name)
            w, h = render(chunk, title, out, font)
            shots.append(dict(file=name, width=w, height=h))
            print("  %-28s %4dx%-5d %s" % (name, w, h, title))

        index.append(dict(title=item["title"], note=item["note"], shots=shots))

    with open(os.path.join(SHOT_DIR, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
    print("\n共生成 %d 张截图，保存在 report/screenshots/"
          % sum(len(i["shots"]) for i in index))


if __name__ == "__main__":
    sys.exit(main())
