#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把实验结果、截图与实验总结写回实验报告 Word 文档。

运行：python3 report/fill_report.py
输入：report/screenshots/*.png
输出：实验一 基于文件系统的商城库存管理系统.docx（在原文档上填写）
"""

import json
import os
import sys

import docx
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCX = os.path.join(ROOT, "实验一 基于文件系统的商城库存管理系统.docx")
SHOTS = os.path.join(ROOT, "report", "screenshots")

MAX_W = 5.70        # 正文可用宽度（A4，左右页边距 1.25 英寸）
MAX_H = 7.00        # 图片最大显示高度（控制高度可以减少排版时留下的大片空白）
DOC_MARK = "（一）数据集的组成与数据文件的存储形式"


def style_run(run, size=10.5, bold=False, italic=False,
              latin="Times New Roman", ea="宋体", color=None):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.name = latin
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.get_or_add_rFonts()
    rfonts.set(qn("w:eastAsia"), ea)
    rfonts.set(qn("w:hint"), "eastAsia")
    if color is not None:
        run.font.color.rgb = color
    return run


def add_para(cell, text, size=10.5, bold=False, indent=0, align=None,
             space_before=0, space_after=3, line_spacing=1.35):
    p = cell.add_paragraph()
    pf = p.paragraph_format
    pf.space_before = Pt(space_before)
    pf.space_after = Pt(space_after)
    pf.line_spacing = line_spacing
    if indent:
        pf.left_indent = Pt(indent)
    if align is not None:
        p.alignment = align
    if text:
        style_run(p.add_run(text), size=size, bold=bold)
    return p


def add_image(cell, filename, caption, fig_no, max_w=MAX_W, max_h=MAX_H):
    path = os.path.join(SHOTS, filename)
    if not os.path.exists(path):
        raise SystemExit("找不到截图：%s" % path)

    from PIL import Image
    with Image.open(path) as im:
        w, h = im.size
    width = min(max_w, max_h * w / h)
    width = max(width, 3.2)

    p = cell.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(2)
    p.add_run().add_picture(path, width=Inches(width))

    cap = cell.add_paragraph()
    cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
    cap.paragraph_format.space_before = Pt(0)
    cap.paragraph_format.space_after = Pt(8)
    style_run(cap.add_run("图 %d  %s" % (fig_no, caption)), size=9,
              color=RGBColor(0x40, 0x40, 0x40))


def clear_cell(cell):
    """保留段落格式，清空单元格中原有的文字。"""
    for p in list(cell.paragraphs):
        for r in list(p.runs):
            r._element.getparent().remove(r._element)


# ---------------------------------------------------------------------------
# 各部分的文字内容
# ---------------------------------------------------------------------------
ENV_LINES = [
    "计算机配置：Intel Xeon Platinum（x86_64 架构）处理器，2 核 vCPU，内存 1.6 GB，系统盘 40 GB；",
    "操作系统：Ubuntu 24.04 LTS（Linux 内核 6.8.0-63-generic，64 位）；",
    "编程语言：C++（C++17 标准，使用 <filesystem>、<fstream>、<algorithm>、<map> 等标准库，不依赖任何第三方库）；",
    "编程工具：g++ 13.3.0 编译器、GNU Make 4.3 构建工具、Vim 编辑器、bash 5.2 终端；",
    "数据存储：操作系统文件系统上的两个纯文本文件 data/products.txt（商品目录）与 data/records.txt（进销记录）；",
    "编译与运行：g++ -std=c++17 -O2 -Wall -Wextra src/inventory.cpp -o inventory，运行 ./inventory 进入交互式菜单；"
    "数据集可由 ./inventory --init 重新生成。",
]

RESULT_BLOCKS = [
    ("h", "（一）数据集的组成与数据文件的存储形式"),
    ("p", "本实验的数据集由本人根据系统需求自行设计和生成，包含 5 个类别"
          "（手机数码、家用电器、食品饮料、图书文具、服饰鞋包）、15 种商品，"
          "以及 2026-03-01 至 2026-09-20 期间的 175 条进货与销售记录，"
          "操作人包括张三、李四、王五、赵六。数据由程序的 --init 功能按固定随机种子生成，可重复获得。"),
    ("p", "数据不使用任何数据库，而是分别保存在操作系统文件系统的两个纯文本文件中，一行一条记录，"
          "字段之间用“|”分隔，因此可以直接用文本编辑器查看、备份，也可以用 wc、head 等命令统计："),
    ("li", "data/products.txt —— 商品目录（主数据），字段为「商品编号|商品名称|类别|单价|库存量」；"),
    ("li", "data/records.txt —— 进销记录（流水数据），字段为「记录号|商品编号|商品名称|操作类型|操作人|操作时间|操作数量|单价」，"
           "其中操作类型 P 表示进货、S 表示销售。"),
    ("p", "两个文件之间的关联通过“商品编号”建立：记录中的商品编号指向商品目录中的商品，"
          "同时把商品名称与单价冗余写入记录，使商品被删除后，记录仍然可以独立读懂、独立统计。"
          "下面是数据文件在磁盘上的实际内容与规模："),
    ("img", "13_data_files.png", "数据文件在文件系统中的存储形式（纯文本、一行一条记录、字段以“|”分隔）"),

    ("h", "（二）商品目录查看（实验内容第 1 项）"),
    ("p", "在菜单中选择 1，程序把商品目录文件读入内存后按“类别”分组，逐类输出该类别下的全部商品，"
          "并给出每个类别的商品种数与库存小计，最后给出全目录的类别数、商品种数与库存总计，"
          "从而实现了“商品按类别进行组织和展示”的要求。图中可以看到 5 个类别分别成组显示。"),
    ("img", "01_catalog_1.png", "商品目录查看：按类别分组展示（第 1 页，显示前 3 个类别）"),
    ("img", "01_catalog_2.png", "商品目录查看：按类别分组展示（第 2 页，显示后 2 个类别与全目录汇总）"),

    ("h", "（三）库存管理——进货（实验内容第 2 项）"),
    ("p", "在菜单中选择 2，输入商品编号、进货数量、操作人和操作时间，程序先在商品目录中查找该商品并显示其名称、"
          "类别、单价与当前库存，然后把库存增加相应数量并写回商品目录文件，同时向进销记录文件追加一条记录"
          "（包含商品编号、商品名称、操作类型、操作人、操作时间和操作数量）。"
          "下图中对 P1002 蓝牙耳机 B2 进货 60 件，库存由 106 件变为 166 件，并生成了记录号为 1176 的进货记录。"),
    ("img", "02_purchase.png", "进货：按商品编号增加库存并写入一条进销记录"),

    ("h", "（四）库存管理——销售（实验内容第 2 项）"),
    ("p", "在菜单中选择 3 进行销售，处理过程与进货相同，区别在于库存做减法，并且必须先检查库存是否足够。"
          "下图对 P1002 销售 25 件，操作人李四，库存由 166 件减少到 141 件，并生成记录号 1177 的销售记录。"),
    ("img", "03_sale.png", "销售：按商品编号减少库存并写入一条进销记录"),

    ("h", "（五）录入数据的正确性检查（实验内容的“其他要求”）"),
    ("p", "程序对所有录入数据都做了合法性检查，非法输入会被拒绝并要求重新输入，不会写入文件。"
          "下图中依次演示了：① 商品编号不存在（P9999）；② 销售数量超过当前库存（99999 件大于库存 141 件）；"
          "③ 数量不是正整数（12.5、abc、-5）；④ 数量为 0；⑤ 时间格式非法（2026-13-45 99:00:00，"
          "月份、日期、小时、分钟都超出合法范围）。只有最后一次合法输入才被接受并写入文件。"),
    ("img", "04_validation_1.png", "数据正确性检查：商品编号不存在、库存不足被拒绝（第 1 页）"),
    ("img", "04_validation_2.png", "数据正确性检查：数量与时间格式校验，非法输入被拒绝后重输（第 2 页）"),

    ("h", "（六）按类别浏览商品并按库存量排序（实验内容第 4 项）"),
    ("p", "在菜单中选择 5，程序列出全部类别供选择，也可以输入 0 浏览全部类别。"
          "选定类别后，程序只取出该类别下的商品，按库存量从多到少排序输出，并给出名次、库存合计。"
          "下图选择“手机数码”，可以看到 3 种商品按库存量 221、149、106 降序排列。"),
    ("img", "05_browse.png", "按类别浏览商品：只显示所选类别的商品，并按库存量降序排序"),

    ("h", "（七）进销记录查询（实验内容第 5 项）"),
    ("p", "在菜单中选择 6，先输入商品编号，再分别输入起始时间、结束时间和操作人；"
          "起始/结束时间只写“YYYY-MM-DD”时，程序自动补齐为当天的 00:00:00 与 23:59:59，"
          "直接回车表示该条件不限制。程序逐行扫描进销记录文件，输出满足条件的记录，"
          "并按时间排序，最后汇总进货总量、销售总量与销售金额。"),
    ("img", "06_query_time.png", "进销记录查询：按时间范围（2026-06-01 ~ 2026-09-21）检索某一商品的记录"),
    ("img", "07_query_user.png", "进销记录查询：按操作人（张三）检索某一商品的记录"),

    ("h", "（八）销量汇总（实验内容第 6 项）"),
    ("p", "在菜单中选择 7，先指定统计时间范围，再选择统计范围（输入类别名称统计该类商品，直接回车统计全部商品）。"
          "程序只统计销售类型的记录，按商品汇总销量并降序输出，同时给出参与统计的商品种数、总销量和总销售额。"),
    ("img", "08_summary_cat.png", "销量汇总：统计某类商品（食品饮料）在指定时间范围内的销量"),
    ("img", "09_summary_all.png", "销量汇总：统计全部商品在 2026-03-01 ~ 2026-09-30 期间的销量"),

    ("h", "（九）商品删除，并保留该商品的进销记录（实验内容第 3 项）"),
    ("p", "在菜单中选择 4 删除商品。程序先显示待删除商品的信息，并统计该商品在进销记录文件中已有的记录条数与"
          "进销数量，再要求确认。确认后程序只从商品目录文件中删除这一行，进销记录文件不做任何修改，"
          "因此该商品的历史进货、销售记录被完整保留下来，之后仍然可以正常查询与统计。"),
    ("img", "10_delete.png", "商品删除：删除前提示该商品已有 12 条历史记录，删除后记录文件不做任何修改"),
    ("img", "11_query_deleted.png", "商品删除后再次查询：程序提示商品已删除，但仍完整返回其 12 条历史进销记录"),

    ("h", "（十）新增商品（数据集维护，附加功能）"),
    ("p", "菜单中选择 8 可以向商品目录中新增商品。程序要求商品编号不能重复、名称与类别不能为空、"
          "单价必须为大于 0 的数、库存量必须为正整数，校验通过后才把新商品追加写入商品目录文件。"),
    ("img", "12_add_product.png", "新增商品：校验通过后把新商品写入商品目录文件"),

    ("h", "（十一）结果分析"),
    ("p", "1．数据结构的选择。程序为商品和进销记录分别定义了 Product 与 Record 结构体，"
          "读取时把文件内容解析到 vector 中，在内存里完成查找、排序、汇总，再把结果写回文件。"
          "商品种数很少，这种“整体读入—处理—整体写回”的方式实现简单、逻辑清晰；"
          "而记录文件只在末尾追加，避免了每次操作都重写全部历史数据。"),
    ("p", "2．数据之间关联的表达。文件系统本身没有表、字段和关联的概念，"
          "本实验用“商品编号”作为两个文件之间的关联键：商品目录中的编号是唯一的，"
          "进销记录中的商品编号指向它。由于文件系统不能提供外键约束，"
          "关联的正确性完全由程序保证（例如进货/销售前必须先确认编号存在）。"),
    ("p", "3．为什么要冗余保存商品名称与单价。题目要求删除商品后仍保留其进销记录，"
          "如果记录中只保存商品编号，商品删除后记录就无法读出商品名。"
          "因此本实验在记录中冗余保存了商品名称与单价，用少量冗余换取了历史记录的独立性，"
          "这也正是文件系统缺少“视图/连接”能力时常用的解决办法。"),
    ("p", "4．查询与统计的效率。所有查询都是把文件顺序读入后逐条过滤，时间复杂度为 O(n)，"
          "没有索引可用；对 175 条记录的数据量而言完全可以接受，但当记录数增长到数十万条时，"
          "全表扫描就会成为瓶颈，需要引入索引文件或改用数据库。"),
    ("p", "5．健壮性。程序对编号存在性、数量取值、库存是否充足、时间格式与取值范围、"
          "分隔符冲突等都做了检查，保证了写入文件的数据始终是可解析的，"
          "读取时也会跳过字段数不足或数值无法解析的“坏行”，避免一处损坏导致整个程序无法运行。"),
]

SUMMARY_BLOCKS = [
    ("p", "本实验仅使用操作系统提供的文件系统（两个纯文本文件）就完成了商品目录维护、进货、销售、"
          "删除商品、按类别浏览排序、进销记录查询和销量汇总等功能，"
          "通过实践对“用文件存取数据、用编号表达数据间关联”有了直观的认识。"
          "以下是使用文件实现数据增删改查的优缺点总结。"),
    ("h", "一、使用文件实现数据增删改查的优点"),
    ("li", "1．实现简单，不依赖数据库系统。不需要安装、配置数据库服务，也不需要建表、连接和权限管理，"
           "程序只用标准库的 ifstream / ofstream 就能读写数据，代码量小、理解成本低，便于移植。"),
    ("li", "2．数据可读性强、便于交换与备份。数据以纯文本保存，一行一条记录、字段用分隔符隔开，"
           "可以直接用文本编辑器打开查看，也可以用 wc、head 等命令统计，拷贝文件即完成备份与迁移。"),
    ("li", "3．存取控制灵活。程序可以自己决定数据的组织方式（字段顺序、分隔符、文件名），"
           "也可以在同一个程序里同时使用多个文件保存不同性质的数据（如本实验把“商品目录”与“进销流水”分开存放）。"),
    ("li", "4．对小型数据足够快。商品目录只有十几条、记录不到两百条时，读入内存再处理几乎是瞬时的。"),
    ("h", "二、使用文件实现数据增删改查的缺点"),
    ("li", "1．数据冗余与一致性难以保证。为了让记录在商品删除后依然可读，必须把商品名称、单价冗余写入记录；"
           "一旦商品改名或调价，历史记录与当前目录之间就会出现不一致，只能靠程序约定和维护。"),
    ("li", "2．缺少约束机制。文件系统没有主键、外键、非空、唯一性等约束，"
           "关联关系完全靠程序保证，容易产生“孤立的记录”（如商品已删除而记录仍存在）或重复编号，"
           "本实验不得不专门编写校验代码来弥补。"),
    ("li", "3．查询效率低、无法建立索引。查询、汇总都要全文件扫描，复杂度为 O(n)；"
           "数据量增大后性能急剧下降，也无法像数据库那样用索引、哈希或 B+ 树加速。"),
    ("li", "4．修改（改、删）的代价大。商品的修改与删除是“整体读入—修改—整体写回”，"
           "写放大严重；一旦在写回过程中程序异常退出或断电，文件可能被写坏，"
           "记录的插入、删除也只能靠整表重写或标记删除来模拟。"),
    ("li", "5．并发访问不安全。多个用户或进程同时读写同一个文件时没有并发控制，"
           "后写入的会覆盖先写入的内容，也没有事务、回滚和日志恢复机制。"),
    ("li", "6．数据类型弱。数值、日期时间都以字符串形式保存，类型检查和范围检查只能由程序完成"
           "（本实验就专门实现了时间格式、库存数量、编号存在性等校验），"
           "排序和比较也往往退化为字符串比较。"),
    ("li", "7．安全性与共享性差。文件权限由操作系统统一管理，难以做到“按字段、按记录”的访问控制，"
           "也不便于多用户协同使用。"),
    ("h", "三、改进方向"),
    ("p", "在上述缺点中，效率与一致性问题可以通过技术手段缓解，例如：为常用查询建立索引文件；"
          "采用定长二进制记录支持随机定位与就地修改；用“临时文件 + 原子改名”保证写回过程的安全性；"
          "用“标记删除”代替物理删除以减少写放大。但当数据规模、并发程度和一致性要求进一步提高时，"
          "这些办法会逐渐变得复杂而脆弱，此时更合理的做法是把数据交给数据库系统管理，"
          "由 DBMS 负责索引、事务、并发控制和完整性约束。这也正是本实验之后要学习数据库技术的原因："
          "文件系统适合保存小规模、单用户、弱一致性的数据，而数据库系统解决的是大规模、多用户、强一致性下的数据管理问题。"),
]


def build_blocks(cell, blocks, fig_counter):
    for b in blocks:
        kind = b[0]
        if kind == "h":
            add_para(cell, b[1], size=10.5, bold=True, space_before=6, space_after=3)
        elif kind == "p":
            add_para(cell, b[1], size=10.5)
        elif kind == "li":
            p = add_para(cell, b[1], size=10.5)
            p.paragraph_format.left_indent = Pt(18)
        elif kind == "img":
            add_image(cell, b[1], b[2], fig_counter[0])
            fig_counter[0] += 1


def main():
    if not os.path.exists(DOCX):
        raise SystemExit("找不到实验报告文档：%s" % DOCX)

    # 首次填写前保存一份空白模板，便于以后重新生成报告
    backup = os.path.join(ROOT, "report", "实验报告-空白模板备份.docx")
    if not os.path.exists(backup):
        docx.Document(DOCX).save(backup)
        print("已保存空白模板备份：%s" % backup)

    # 若文档已被填写过，则从空白模板重新填写，保证脚本可以重复运行
    source = DOCX
    probe = docx.Document(DOCX)
    if any(DOC_MARK in p.text for p in probe.tables[0].rows[4].cells[0].paragraphs):
        source = backup
        print("检测到文档已填写过，改为从空白模板重新生成 ...")
    doc = docx.Document(source)
    table = doc.tables[0]

    # 题目
    clear_cell(table.rows[1].cells[1])
    style_run(table.rows[1].cells[1].paragraphs[0].add_run(
        "基于文件系统的“商城库存管理”应用系统"), size=10.5)

    # 姓名、学号留空由本人填写（这里保持空白）

    # 实验环境
    env_cell = table.rows[3].cells[0]
    for line in ENV_LINES:
        add_para(env_cell, line, size=10.5)

    # 实验结果及分析
    res_cell = table.rows[4].cells[0]
    build_blocks(res_cell, RESULT_BLOCKS, [1])

    # 实验总结
    sum_cell = table.rows[5].cells[0]
    build_blocks(sum_cell, SUMMARY_BLOCKS, [999])

    doc.save(DOCX)
    print("实验报告已填写完成：%s" % DOCX)


if __name__ == "__main__":
    sys.exit(main())
