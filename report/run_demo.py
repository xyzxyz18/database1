#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
演示与录制脚本。

在伪终端(pty)中驱动 ./inventory，逐个演示实验要求中的每一项功能，
把每一次会话（含命令回显）原样保存为 report/transcripts/*.txt，
供 render_transcripts.py 渲染成实验报告用的“输入输出截图”。

运行：python3 report/run_demo.py
"""

import fcntl
import json
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRANS_DIR = os.path.join(ROOT, "report", "transcripts")
COLS, ROWS = 118, 60
MENU_MARK = "================= 商城库存管理系统（基于文件系统） ================="


def run_pty(argv, inputs, idle=0.30, timeout=90):
    """在 pty 中运行 argv，按顺序把 inputs 当作一行行输入发送，返回全部输出。"""
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ,
                struct.pack("HHHH", ROWS, COLS, 0, 0))
    proc = subprocess.Popen(argv, cwd=ROOT, stdin=slave, stdout=slave,
                            stderr=slave, close_fds=True)
    os.close(slave)

    buf = bytearray()
    pending = list(inputs)
    last = time.time()
    start = time.time()

    while True:
        if time.time() - start > timeout:
            break
        ready, _, _ = select.select([master], [], [], 0.05)
        if ready:
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            if not data:
                break
            buf += data
            last = time.time()
        else:
            quiet = time.time() - last
            if pending and quiet > idle:
                os.write(master, (pending.pop(0) + "\n").encode("utf-8"))
                last = time.time()
            elif not pending and quiet > idle and proc.poll() is not None:
                break
            elif not pending and quiet > 3.0:
                break

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    os.close(master)

    text = buf.decode("utf-8", "replace")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text


def trim_trailing_menu(text):
    """去掉最后一次功能执行后重新打印的菜单，让截图只保留本次功能的输入输出。"""
    pos = text.rfind(MENU_MARK)
    if pos > 0:
        text = text[:pos].rstrip() + "\n"
    return text


SESSIONS = [
    dict(file="01_catalog.txt",
         title="功能一  商品目录查看（商品按类别组织展示）",
         note="菜单选择 1，输出按类别分组，并给出每个类别的商品数与库存小计。",
         inputs=["1", "0"]),
    dict(file="02_purchase.txt",
         title="功能二  库存管理——进货（增加库存并写入进销记录）",
         note="对 P1002 蓝牙耳机 B2 进货 60 件，操作人张三，库存由 106 件增加到 166 件，同时写入一条记录。",
         inputs=["2", "P1002", "60", "张三", "2026-09-21 10:00:00", "0"]),
    dict(file="03_sale.txt",
         title="功能三  库存管理——销售（减少库存并写入进销记录）",
         note="对 P1002 销售 25 件，操作人李四，库存由 166 件减少到 141 件，同时写入一条记录。",
         inputs=["3", "P1002", "25", "李四", "2026-09-21 11:20:00", "0"]),
    dict(file="04_validation.txt",
         title="功能四  录入数据的正确性检查（非法输入被拒绝并提示重输）",
         note="依次演示：商品编号不存在、销售数量超过库存、数量不是正整数（12.5 / abc / -5）、数量为 0、时间格式非法，全部被拦截，最后才接受合法输入。",
         inputs=["2", "P9999",
                 "3", "P1002", "99999",
                 "2", "P1002", "12.5", "abc", "-5", "0", "80",
                 "王五", "2026-13-45 99:00:00", "2026-09-21 14:00:00",
                 "0"]),
    dict(file="05_browse.txt",
         title="功能五  按类别浏览商品（按库存量从多到少排序）",
         note="选择类别“手机数码”，输出按库存量降序排列并给出排名与库存合计。",
         inputs=["5", "3", "0"]),
    dict(file="06_query_time.txt",
         title="功能六  进销记录查询（按时间范围检索）",
         note="查询 P1002 在 2026-06-01 ~ 2026-09-21 之间的全部进销记录。",
         inputs=["6", "P1002", "2026-06-01", "2026-09-21", "", "0"]),
    dict(file="07_query_user.txt",
         title="功能六（续）  进销记录查询（按操作人检索）",
         note="查询 P1002 由操作人“张三”办理的全部进销记录。",
         inputs=["6", "P1002", "", "", "张三", "0"]),
    dict(file="08_summary_cat.txt",
         title="功能七  销量汇总（某类商品、指定时间范围）",
         note="统计 2026-06-01 ~ 2026-08-31 类别“食品饮料”的销量，按销量降序并给出总销量与总销售额。",
         inputs=["7", "2026-06-01", "2026-08-31", "食品饮料", "0"]),
    dict(file="09_summary_all.txt",
         title="功能七（续）  销量汇总（全部商品、指定时间范围）",
         note="统计 2026-03-01 ~ 2026-09-30 全部商品的销量。",
         inputs=["7", "2026-03-01", "2026-09-30", "", "0"]),
    dict(file="10_delete.txt",
         title="功能八  商品删除（删除后保留该商品的进销记录）",
         note="删除 P1001 智能手机 A1，程序先提示该商品已有 12 条历史记录，删除商品后记录文件不做任何修改。",
         inputs=["4", "P1001", "y", "0"]),
    dict(file="11_query_deleted.txt",
         title="功能八（续）  商品已被删除，其历史进销记录仍可查询",
         note="再次查询 P1001，程序给出“该商品已从商品目录中删除，其历史进销记录仍然保留”的提示，并完整返回 12 条记录。",
         inputs=["6", "P1001", "", "", "", "0"]),
    dict(file="12_add_product.txt",
         title="功能九  新增商品（数据集维护）",
         note="新增商品 P6001 折叠雨伞，类别“日用百货”，单价 39.90 元，初始库存 100 件。",
         inputs=["8", "P6001", "折叠雨伞", "日用百货", "39.90", "100", "0"]),
]


def main():
    os.makedirs(TRANS_DIR, exist_ok=True)

    print(">>> 重新生成演示数据集 ...")
    subprocess.run(["./inventory", "--init"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)

    meta = []
    for s in SESSIONS:
        print(">>> 录制：%s" % s["title"])
        text = run_pty(["./inventory"], s["inputs"])
        text = trim_trailing_menu(text)
        with open(os.path.join(TRANS_DIR, s["file"]), "w", encoding="utf-8") as f:
            f.write(text)
        meta.append(dict(file=s["file"], title=s["title"], note=s["note"]))

    # 数据文件内容演示（用 shell 命令展示底层存储，不是程序输出）
    print(">>> 录制：数据文件内容")
    script = (
        r'echo "$ ls -l data/"' + "\n"
        r'ls -l data/' + "\n"
        r'echo' + "\n"
        r'echo "$ head -n 6 data/products.txt"' + "\n"
        r'head -n 6 data/products.txt' + "\n"
        r'echo' + "\n"
        r'echo "$ head -n 10 data/records.txt"' + "\n"
        r'head -n 10 data/records.txt' + "\n"
        r'echo' + "\n"
        r'echo "$ wc -l data/products.txt data/records.txt"' + "\n"
        r'wc -l data/products.txt data/records.txt' + "\n"
    )
    text = run_pty(["bash", "-c", script], [])
    text = text.strip() + "\n"
    with open(os.path.join(TRANS_DIR, "13_data_files.txt"), "w",
              encoding="utf-8") as f:
        f.write(text)
    meta.append(dict(file="13_data_files.txt",
                     title="数据文件的物理存储形式（纯文本、一行一条记录、字段用 | 分隔）",
                     note="商品目录与进销记录分别存放在两个文本文件中，可直接用文本工具查看与统计。"))

    with open(os.path.join(TRANS_DIR, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)

    print("\n完成：共 %d 段会话，保存在 report/transcripts/" % len(meta))


if __name__ == "__main__":
    sys.exit(main())
